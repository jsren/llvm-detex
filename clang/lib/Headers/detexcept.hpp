/* (c) 2021 James Renwick */
// void call()
// {
//     throw 1;
// }
// int main()
// {
//     try {
//         call();
//     }
//     catch (int) {

//     }
//     catch (...) {

//     }
// }

/// If 1, allows exception object allocations to spill onto the
/// heap when the dedicated exception object buffer is exhausted.
/// Otherwise OOM will trigger std::terminate.
#define __CXA_EXCEPTION_HEAP_SPILL 0

/// If 1, allocate an exception object buffer for each thread via TLS
#define __CXA_EXCEPTION_BUFFER_THREADSAFE 0

/// If 1, allow exception objects to be over-aligned
#define __CXA_EXCEPTION_OBJECT_OVERALIGN 0


#if __CXA_EXCEPTION_BUFFER_THREADSAFE
#define __CXA_EOB_THREADLOCAL thread_local
#else
#define __CXA_EOB_THREADLOCAL
#endif

inline constexpr decltype(sizeof(int)) __cxa_eob_size = 1024;

__CXA_EOB_THREADLOCAL static char __cxa_execption_object_buffer[__cxa_eob_size];
__CXA_EOB_THREADLOCAL static char* __cxa_execption_object_buffer_ptr = __cxa_execption_object_buffer;

enum __cxa_exception_flags {
    __cxa_EXCEPTION_SBO = 1,
    __cxa_EXCEPTION_POINTER = 2,
};

struct __cxa_typeinfo {
    const __cxa_typeinfo** base_types;
    decltype(sizeof(int)) size;
    void (*copy_ctor)(char* _this, const char* src);
    void (*dtor)(char* _this);
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
    unsigned char alignment;
#endif
};

struct __cxa_exception_metadata {
    const __cxa_typeinfo* exception_typeinfo;
};

struct __cxa_exception_state_alloc {
    const __cxa_typeinfo* exception_typeinfo;
    char* object;
    unsigned char flags[sizeof(void*)];
};

struct __cxa_exception_state_sbo {
    const __cxa_typeinfo* exception_typeinfo;
    char object[sizeof(void*) * 3];
};

union __cxa_exception_state {
    __cxa_exception_state_alloc alloc;
    __cxa_exception_state_sbo sbo;

    [[gnu::always_inline]]
    inline constexpr bool active() const noexcept {
        return this->alloc.exception_typeinfo;
    }
    [[gnu::always_inline]]
    inline constexpr auto typeinfo() const noexcept {
        return this->alloc.exception_typeinfo;
    }
    [[gnu::always_inline]]
    inline char flags() const noexcept {
        // TODO: This is probably UB
        return reinterpret_cast<const char*>(this)[sizeof(*this) - 1];
    }
    [[gnu::always_inline]]
    inline void set_flags(char flags) noexcept {
        // TODO: This is probably UB
        reinterpret_cast<char*>(this)[sizeof(*this) - 1] = flags;
    }
};

static_assert(sizeof(__cxa_exception_state) == sizeof(void*) * 4);
static_assert(__has_trivial_destructor(__cxa_exception_state_alloc));
static_assert(__has_trivial_destructor(__cxa_exception_state_sbo));

template<typename T>      /* Use the top byte for the is_pointer and is_trivial flags */
constexpr bool _can_sbo = sizeof(T) < sizeof(__cxa_exception_state_sbo::object) &&
                          (alignof(__cxa_exception_state_sbo) % alignof(T)) == 0 &&
                          (__builtin_offsetof(__cxa_exception_state_sbo, object) % alignof(T)) == 0;

template<typename T>
struct _is_pointer_t {
    static constexpr bool value = false;
};
template<typename T>
struct _is_pointer_t<T*> {
    static constexpr bool value = true;
};

template<typename T>
constexpr __cxa_typeinfo __cxa_create_typeinfo()
{
    auto log2 = [](auto value) constexpr { return static_cast<char>(31 - __builtin_clz(value)); };
    constexpr char align = log2(alignof(T));

    if constexpr (__is_trivially_constructible(T, const T&)) {
        if constexpr (__has_trivial_destructor(T)) {
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
            return {nullptr, sizeof(T), nullptr, nullptr, align};
#else
            return {nullptr, sizeof(T), nullptr, nullptr};
#endif
        } else {
            auto dtor = [](T& t) { t->~T(); };
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
            return {nullptr, sizeof(T), nullptr, &dtor, align};
#else
            return {nullptr, sizeof(T), nullptr, &dtor};
#endif
        }
    }
    else {
        auto ctor = [](T* t, const T& other) { return new (t) T(other); };
        if constexpr (__has_trivial_destructor(T)) {
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
            return {nullptr, sizeof(T), ctor, nullptr, align};
#else
            return {nullptr, sizeof(T), ctor, nullptr};
#endif
        } else {
            auto dtor = [](T& t) { t->~T(); };
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
            return {nullptr, sizeof(T), ctor, &dtor, align};
#else
            return {nullptr, sizeof(T), ctor, &dtor};
#endif
        }
    }
}


template<typename T>
inline constexpr __cxa_typeinfo _typeinfo_for = __cxa_create_typeinfo<T>();


namespace __cxa_std {
    template<typename T> struct remove_reference { typedef T type; };
    template<typename T> struct remove_reference<T&> { typedef T type; };
    template<typename T> struct remove_reference<T&&> { typedef T type; };

    template<typename T>
    constexpr inline T&& forward(typename remove_reference<T>::type& ref) noexcept
    {
        return static_cast<T&&>(ref);
    }
    template<typename T>
    constexpr inline T&& forward(typename remove_reference<T>::type&& ref) noexcept
    {
        return static_cast<T&&>(ref);
    }
}

void *operator new (decltype(sizeof(int)), void* address) noexcept
{
    return address;
}

namespace std
{
    [[noreturn]] void terminate() noexcept;
}

template<typename T>
[[gnu::always_inline]]
const T& __cxa_get_exception_object(const __cxa_exception_state* eso) noexcept {
    if constexpr (_can_sbo<T>) {
        return *reinterpret_cast<const T*>(eso->sbo.object);
    } else {
        return *reinterpret_cast<const T*>(eso->alloc.object);
    }
}

struct __cxa_exception_alloc_header {
    decltype(sizeof(int)) total_size;
};
// We must memcpy this so that we can store it without worrying about alignment
// i.e. memcpy into allocation, then memcpy out before accessing
static_assert(__is_trivially_constructible(__cxa_exception_alloc_header, __cxa_exception_alloc_header));

#if __CXA_EXCEPTION_HEAP_SPILL
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
extern "C" void* aligned_alloc(decltype(sizeof(int)) alignment, decltype(sizeof(int)) size);
#else
extern "C" void* malloc(decltype(sizeof(int)) size);
#endif
extern "C" void free(void* ptr);
#endif


char* __cxa_alloc_exception_object(decltype(sizeof(int)) size, unsigned char alignment) noexcept
{
    // Probably safe to put header directly after the exception object
    // since the user should never be accessing it from within the buffer
    auto ptr = __cxa_execption_object_buffer_ptr;

#if __CXA_EXCEPTION_OBJECT_OVERALIGN
    auto align = 1 << alignment;
#else
    constexpr auto align = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    (void)alignment;
#endif
    auto offset = reinterpret_cast<unsigned long long>(ptr) & (align - 1);
    if (offset) {
        ptr += align - offset;
        size += align - offset;
    }
    // If the allocation will fit, store the "header" and return
    if ((ptr + sizeof(__cxa_exception_alloc_header)) - __cxa_execption_object_buffer <= sizeof(__cxa_execption_object_buffer)) {
        __cxa_exception_alloc_header header{size + sizeof(__cxa_exception_alloc_header)};
        __builtin_memcpy(ptr + size, &header, sizeof(__cxa_exception_alloc_header));
        return ptr;
    }
    // Handle OOM on the exception object buffer
    else {
#if __CXA_EXCEPTION_HEAP_SPILL
        // If we're allowing heap spill, fall back to heap allocation
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
        ptr = reinterpret_cast<char*>(aligned_alloc(align, size + sizeof(__cxa_exception_alloc_header)));
#else
        ptr = reinterpret_cast<char*>(malloc(size + sizeof(__cxa_exception_alloc_header)));
#endif
        // Store "header" after the allocation - use size = 0 to indicate heap storage
        __cxa_exception_alloc_header header{0};
        __builtin_memcpy(ptr + size, &header, sizeof(__cxa_exception_alloc_header));
        return ptr;
#else
        // Otherwise terminate
        std::terminate();
#endif
    }
}

void __cxa_free_exception_object(char* object, decltype(sizeof(int)) size, unsigned char alignment) noexcept
{
    // First read the header
    __cxa_exception_alloc_header header;
    __builtin_memcpy(object + size, &header, sizeof(__cxa_exception_alloc_header));

    __cxa_execption_object_buffer_ptr -= header.total_size;
    if (header.total_size) {
        return;
    } else {
#if __CXA_EXCEPTION_HEAP_SPILL
        free(object);
#endif
    }
}

template<typename T>
void __cxa_zcthrow(__cxa_exception_state* __exception, T&& object) noexcept
{
    new (__exception) __cxa_exception_state;

    if constexpr (_can_sbo<T>) {
        // Pointers should always be SBO
        constexpr auto pointer_flag = _is_pointer_t<T>::value ? __cxa_EXCEPTION_POINTER : 0;
        __exception->sbo.exception_typeinfo = &_typeinfo_for<T>;
        new (__exception->sbo.object) T(__cxa_std::forward<T>(object));
        __exception->set_flags(__cxa_EXCEPTION_SBO | pointer_flag);
    }
    else {
        __exception->alloc.exception_typeinfo = &_typeinfo_for<T>;
        __exception->alloc.object = __cxa_alloc_exception_object(sizeof(object), alignof(decltype(object)));
        new (__exception->alloc.object) T(__cxa_std::forward<T>(object));
        __exception->set_flags(0);
    }
}

template<typename T>
bool __cxa_exception_filter(__cxa_exception_state* __exception) noexcept
{
    // TODO: Handle pointers and references correctly
    auto typeinfo = __exception->typeinfo();
    if (&_typeinfo_for<T> == typeinfo) return true;
    for (const __cxa_typeinfo** ti = typeinfo->base_types; ti != nullptr; ti++) {
        if (*ti == typeinfo) return true;
    }
    return false;
}

[[gnu::always_inline]]
void __cxa_exception_state_init(__cxa_exception_state* __exception) noexcept
{
    __exception->alloc.exception_typeinfo = 0;
}

[[gnu::always_inline]]
bool __cxa_exception_state_active(const __cxa_exception_state* __exception) noexcept
{
    return __exception->active();
}
[[gnu::always_inline]]
bool __cxa_exception_object_alloc_size(const __cxa_exception_state* __exception) noexcept
{
    if (__exception->flags() == __cxa_EXCEPTION_SBO) {
        return 0; // Already allocated inline - no need to alloc new memory
    } else {
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
        return __exception->typeinfo()->size + __exception->typeinfo()->alignment;
#else
        return __exception->typeinfo()->size;
#endif
    }
}

void __cxa_exception_object_move(__cxa_exception_state* __exception, void* dest) noexcept
{
    // First make the copy
    if (__exception->alloc.exception_typeinfo->copy_ctor) {
        __exception->alloc.exception_typeinfo->copy_ctor(reinterpret_cast<char*>(dest), __exception->alloc.object);
    } else {
        __builtin_memcpy(dest, __exception->alloc.object, __exception->alloc.exception_typeinfo->size);
    }
    // Finally destroy the original
    if (__exception->alloc.exception_typeinfo->dtor) {
        __exception->alloc.exception_typeinfo->dtor(__exception->alloc.object);
    }
    if (__exception->flags() != __cxa_EXCEPTION_SBO) {
        __cxa_free_exception_object(__exception->alloc.object, __exception->alloc.exception_typeinfo->size,
#if __CXA_EXCEPTION_OBJECT_OVERALIGN
                                    __exception->alloc.exception_typeinfo->alignment);
#else
                                    0);
#endif
    }
}


void __cxa_propagate_exception(__cxa_exception_state* __exception, __cxa_exception_state* old_exception) noexcept
{
    new (__exception) __cxa_exception_state;

    if (old_exception->flags() & __cxa_EXCEPTION_SBO) {
        __cxa_exception_object_move(old_exception, __exception->sbo.object);
        __exception->sbo.exception_typeinfo = old_exception->typeinfo();
    } else {
        __cxa_exception_object_move(old_exception, __exception->alloc.object);
        __exception->alloc.exception_typeinfo = old_exception->typeinfo();
    }
    __exception->set_flags(old_exception->flags());
}

void __cxa_rethrow(__cxa_exception_state* __exception, __cxa_exception_state* old_exception, __cxa_exception_metadata* meta) noexcept
{
    // Restore typeinfo before rethrow - this will re-activate the exception
    old_exception->alloc.exception_typeinfo = meta->exception_typeinfo;
    __cxa_propagate_exception(__exception, old_exception);
}

[[gnu::always_inline]]
__cxa_exception_metadata __cxa_get_exception_metadata(__cxa_exception_state* __exception) noexcept
{
    return {__exception->typeinfo()};
}

[[gnu::always_inline]]
void __cxa_clear_exception(__cxa_exception_state* __exception) noexcept
{
    __exception->alloc.exception_typeinfo = nullptr;
}

#if __CXA_EXCEPTION_OBJECT_OVERALIGN
void* __cxa_exception_object_alloc_align_pointer(__cxa_exception_state* __exception, void* ptr) noexcept
{
    auto align = (1 << __exception->typeinfo()->alignment);
    auto offset = align - (reinterpret_cast<unsigned long long>(ptr) & (align - 1));
    return reinterpret_cast<char*>(ptr) + offset;
}
#else
[[gnu::always_inline]]
void* __cxa_exception_object_alloc_align_pointer(__cxa_exception_state*, void* ptr) noexcept
{
    return ptr;
}
#endif


// ==============================================================

struct error {
    int id;
    int another;
};

// TOOGLE THIS TO SEE WITHOUT OPTIMISATION
[[gnu::noinline]]
void call(__cxa_exception_state* __exception)
{
    auto i = *((volatile int*)0xC0FFEE);
    if (i) {
        __cxa_zcthrow(__exception, error{12});
    }
}

int main()
{
    __cxa_exception_state __exception;
    __cxa_exception_state_init(&__exception);
    /*try*/
    {__cxa_exception_state __exception1; __cxa_exception_state_init(&__exception1); {

        call(&__exception1);
        if (__builtin_expect(!__cxa_exception_state_active(&__exception1), 1)) {
            return 43;
        }
    }
    // TODO: Figure out how to mark blocks as being cold during CodeGen.
    //       We *100%* need these to be inline-able however otherwise
    //       we won't remove them when they're not necessary!
    /*catch (int e)*/
    if (__cxa_exception_filter<int>(&__exception1))
    {
        auto __exception_metadata = __cxa_get_exception_metadata(&__exception1);
        int e{__cxa_get_exception_object<int>(&__exception1)};
        __cxa_clear_exception(&__exception1);
        return e;
    }
    /*catch (error e)*/
    if (__cxa_exception_filter<error>(&__exception1))
    {
        auto __exception_metadata = __cxa_get_exception_metadata(&__exception1);
        error e{__cxa_get_exception_object<error>(&__exception1)};
        __cxa_clear_exception(&__exception1);
        return e.id * 3;
    }
    /*catch (...)*/ else {
        // Copy exception object into scope - this allows re-throwing
        auto __exception_metadata = __cxa_get_exception_metadata(&__exception1);
        auto size = __cxa_exception_object_alloc_size(&__exception1);
        if (size) {
            auto ptr = __cxa_exception_object_alloc_align_pointer(&__exception1, __builtin_alloca(size));
            __cxa_exception_object_move(&__exception1, ptr);
        }
        // If we rethrow
        __cxa_rethrow(&__exception, &__exception1, &__exception_metadata);
    }
    /* WITHOUT catch-all */
    // If we were in a nested try/throwing function, propagate exception upwards
    //__cxa_propagate_exception(&__exception, &__exception1);
    // We're in a nothrow function (main) so terminate
    //std::terminate();
    }
    return 133;
}
