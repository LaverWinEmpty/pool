#ifndef LWE_POOL_HEADER
#define LWE_POOL_HEADER

#include "aligner.hh"
#include "allocator.hh"
#include "deque.hh"

#include <unordered_set>
#include <concurrent_queue.h>
#include <cstdlib>

/*******************************************************************************
 * pool structure
 *
 * example configuration
 *  - chunk: 96
 *  - align: 32
 *  - count: 2
 *
 * 192                 256                384         512 << address
 *  ├───────┬────┬──────┼──────┬────┬──────┼──────┬────┤
 *  │ block │    │ meta │ data │    │ meta │ data │    │
 *  └───────┼────┼──────┴──────┼────┼──────┴──────┼────┤
 *          └ 24 ┤             ├ 24 ┤             ├ 32 ┘  << padding
 *               └ ─  chunk  ─ ┘    └ ─  chunk  ─ ┘
 *
 * total: 320 byte (64 + 128 * 2)
 *
 * - block : block header (struct) like node
 *   ├─[ 8 byte]: next chunk pointer
 *   ├─[ 8 byte]: next block pointer
 *   ├─[ 8 byte]: prev block pointer
 *   ├─[ 8 byte]: parent pool pointer
 *   └─[32 byte]: padding
 *
 * - chunk: abstract object, not a struct
 *   ├─[ 8 byte]: parent block address (meta)
 *   ├─[96 byte]: actual usable space  (data)
 *   └─[24 byte]: padding
 *
 * NOTE: align is intended for SIMD use and increases capacity.
 * block header padding reason: to ensures alignment for chunk start addresses.
 *
 ******************************************************************************/

/*******************************************************************************
 * how to use
 *
 * 1. use statics
 * - thread local object (lock-free)
 *
 *    type* ptr = pool::statics<sizeof(type)>::construct(...); // malloc
 *    pool<sizeof(type)>::deconstruct<type>(ptr);              // free
 *
 * 2. create object (NOT THREAD-SAFE)
 *
 *    pool p1(sizeof(type), 512, 8);
 *    pool p2(sizeof(type), 256, 32);
 *
 *    type* ptr = p1.construct<type>(...); // malloc "p1"
 *    p2.destruct<type>(ptr);              // free "p2" is ok (not recommended)
 *
 * - automatically finds parent.
 * - but in this case, it goes into gc. (not recommanded reason)
 * - can also use it like this.   
 *
 *    pool::release(ptr); // free
 *
 * - this is for use when the parents are unknown.
 * 
 * gc (garbage collector)
 * - lock-free queue.
 * - push if parents are different.
 * - gc cleanup is call cleanup(), or call destructe().
 * - used for thread-safety.
 *
 * - unused memory can be free by calling cleanup().
 * - NOTE: it is actual memory deallocate.
 * - call at the right time.
 ******************************************************************************/

namespace lwe {
namespace mem {

class pool {
public:
    pool(const pool&)            = delete;
    pool& operator=(const pool&) = delete;
    pool(pool&&)                 = delete;
    pool& operator=(pool&&)      = delete;

private:
    /**
     * @brief memory pool block node
     * @note  4 pointer = 32 byte in x64
     */
    struct block;

public:
    /**
     * @brief thread-safe static memory pool
     * @note  1. use thread_local, it is thread safe but pool count is same as thread count
     * @note  2. different template parameter result in different types
     */
    template<size_t Size, size_t Count = config::DEF_CACHE, size_t Align = config::DEF_ALIGN> class statics {
        static thread_local pool singleton;

    public:
        template<typename T, typename... Args> static T* construct(Args&&...) noexcept;
        template<typename T> static void                 destruct(T*) noexcept;
        static void                                      cleanup() noexcept;
    };

public:
    /**
     * @brief construct a new pool object
     *
     * @param [in] chunk - chunk size, it is padded to the pointer size.
     * @param [in] count - chunk count.
     * @param [in] align - chunk Align, it is adjusted to the power of 2.
     */
    pool(size_t chunk, size_t count = config::DEF_CACHE, size_t align = config::DEF_ALIGN) noexcept ;

public:
    /**
     * @brief destroy the pool object.
     */
    ~pool() noexcept;

public:
    /**
     * @brief get memory, call malloc when top is null and garbage collector is empty.
     */
    template<typename T = void, typename... Args> T* construct(Args&&...) noexcept;

public:
    /**
     * @brief return memory, if not child of pool, push to garbage collector of parent pool.
     */
    template<typename T = void> void destruct(T*) noexcept;

public:
    /**
     * @brief cleaning idle blocks and garbage collector.
     */
    void cleanup() noexcept;

private:
    /**
     * @brief call memory allocate function.
     */
    block* setup() noexcept;

private:
    /**
     * @brief chunk to block.
     */
    void recycle(void*) noexcept;

public:
    /**
     * @brief auto return memory to parent pool
     */
    template<typename T = void> static void release(T*) noexcept;

private:
    /**
     * @brief memory alignment
     * @note  FIRST INTIALIZE ON CONSTRUCTOR
     */
    const size_t ALIGNMENT;

private:
    /**
     * @brief chunk size
     */
    const size_t SIZE;

private:
    /**
     * @brief chunk count
     */
    const size_t COUNT;

private:
    /**
     * @brief allocate unit: block total size
     */
    const size_t ALLOCTATE;

private:
    /**
     * @brief stack: current using block
     */
    block* top = nullptr;

private:
    /**
     * @brief idle blocks
     */
    data::deque<block*> idle;

private:
    /**
     * @brief generated blocks
     */
    std::unordered_set<block*> all;

private:
    /**
     * @brief temp lock-free queue for windows
     */
    Concurrency::concurrent_queue<void*> gc;
};

} // namespace mem
} // namespace lwe

#include "pool.inl"
#endif