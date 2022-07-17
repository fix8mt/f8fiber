//-----------------------------------------------------------------------------------------
// f8_fiber (header only) based on boost::fiber, x86_64 / linux only / de-boosted
// Modifications Copyright (C) 2022 Fix8 Market Technologies Pty Ltd
// see https://github.com/fix8mt/f8fiber
//
// fcontext_t, jump_fcontext, make_fcontext, ontop_fcontext
//	boost::fiber, basic_protected_fixedsize_stack
//          Copyright Oliver Kowalke 2013.
//
// Distributed under the Boost Software License, Version 1.0 August 17th, 2003
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
//
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------------------
#ifndef FIX8_FIBER_HPP_
#define FIX8_FIBER_HPP_

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstddef>
#include <new>
#include <utility>
#include <functional>
#include <iostream>

//-----------------------------------------------------------------------------------------
#if !defined (SIGSTKSZ)
# define SIGSTKSZ 131072 // 128kb recommended
#endif

#if !defined (MINSIGSTKSZ)
# define MINSIGSTKSZ 32768 // 32kb minimum
#endif

//-----------------------------------------------------------------------------------------
namespace FIX8 {

//-----------------------------------------------------------------------------------------
using fcontext_t = void *;
struct fcontext_transfer_t
{
	fcontext_t ctx;
	void *data;
};

struct fcontext_stack_t
{
	void *sptr;
	size_t ssize;
};

//-----------------------------------------------------------------------------------------
namespace
{
	inline size_t getPageSize()
	{
		/* conform to POSIX.1-2001 */
		return static_cast<size_t>(sysconf(_SC_PAGESIZE));
	}
}

//-----------------------------------------------------------------------------------------
/// Anonymous memory mapped region based stack
class f8_protected_fixedsize_stack
{
	std::size_t size_;

public:
	f8_protected_fixedsize_stack(std::size_t size=SIGSTKSZ) noexcept : size_(size) {}

	fcontext_stack_t allocate()
	{
		// calculate how many pages are required
		const std::size_t pages = (size_ + getPageSize() - 1) / getPageSize();
		// add one page at bottom that will be used as guard-page
		const std::size_t size__ = (pages + 1) * getPageSize();

#if defined(USE_MAP_STACK)
		void *vp = ::mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_STACK, -1, 0);
#elif defined(MAP_ANON)
		void *vp = ::mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
		void *vp = ::mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
		if (vp == MAP_FAILED)
			throw std::bad_alloc();

		// conforming to POSIX.1-2001
		::mprotect(vp, getPageSize(), PROT_NONE);
		return { static_cast<char *>(vp) + size__, size__ };
	}

	void deallocate(fcontext_stack_t& sctx) noexcept
	{
		if (sctx.sptr)
		{
			void *vp = static_cast<char *>(sctx.sptr) - sctx.ssize;
			// conform to POSIX.4 (POSIX.1b-1993, _POSIX_C_SOURCE=199309L)
			::munmap(vp, sctx.ssize);
		}
		sctx = {};
	}
};

//-----------------------------------------------------------------------------------------
/// Simple heap based stack
class f8_fixedsize_heap_stack
{
	std::size_t size_;

public:
	f8_fixedsize_heap_stack(std::size_t size=SIGSTKSZ) noexcept : size_(size) {}

	fcontext_stack_t allocate()
	{
		void *vp { ::operator new(size_) };
		return { static_cast<char *>(vp) + size_, size_ };
	}

	void deallocate(fcontext_stack_t& sctx) noexcept
	{
		char *vp { static_cast<char *>(sctx.sptr) - sctx.ssize };
		delete vp;
		sctx = {};
	}
};

//-----------------------------------------------------------------------------------------
extern "C"
{
	fcontext_transfer_t jump_fcontext(const fcontext_t to, void *vp);
	fcontext_t make_fcontext(void *sp, size_t size, void (*fn)(fcontext_transfer_t));
	fcontext_transfer_t ontop_fcontext(const fcontext_t to, void *vp, fcontext_transfer_t (*fn)(fcontext_transfer_t));
}

//-----------------------------------------------------------------------------------------
#if !defined F8FIBER_USE_ASM_SOURCE // default not defined
// note: .weak symbol - if multiple definitions only one symbol will be used
asm(R"(.text
.weak jump_fcontext,make_fcontext,ontop_fcontext
.align 16
.type jump_fcontext,@function
jump_fcontext:
	leaq  -0x38(%rsp), %rsp
	stmxcsr  (%rsp)
	fnstcw   0x4(%rsp)
	movq  %r12,0x8(%rsp)
	movq  %r13,0x10(%rsp)
	movq  %r14,0x18(%rsp)
	movq  %r15,0x20(%rsp)
	movq  %rbx,0x28(%rsp)
	movq  %rbp,0x30(%rsp)
	movq  %rsp,%rax
	movq  %rdi,%rsp
	movq  0x38(%rsp),%r8
	ldmxcsr (%rsp)
	fldcw   0x4(%rsp)
	movq  0x8(%rsp),%r12
	movq  0x10(%rsp),%r13
	movq  0x18(%rsp),%r14
	movq  0x20(%rsp),%r15
	movq  0x28(%rsp),%rbx
	movq  0x30(%rsp),%rbp
	leaq  0x40(%rsp),%rsp
	movq  %rsi,%rdx
	movq  %rax,%rdi
	jmp  *%r8
.size jump_fcontext,.-jump_fcontext
.type make_fcontext,@function
make_fcontext:
	movq  %rdi,%rax
	andq  $-16,%rax
	leaq  -0x40(%rax),%rax
	movq  %rdx,0x28(%rax)
	stmxcsr (%rax)
	fnstcw  0x4(%rax)
	leaq  trampoline(%rip),%rcx
	movq  %rcx,0x38(%rax)
	leaq  finish(%rip),%rcx
	movq  %rcx,0x30(%rax)
	ret
trampoline:
	push %rbp
	jmp *%rbx
finish:
	xorq  %rdi,%rdi
	call  _exit@PLT
	hlt
.size make_fcontext,.-make_fcontext
.type ontop_fcontext,@function
ontop_fcontext:
	movq  %rdx,%r8
	leaq  -0x38(%rsp),%rsp
	stmxcsr (%rsp)
	fnstcw  0x4(%rsp)
	movq  %r12,0x8(%rsp)
	movq  %r13,0x10(%rsp)
	movq  %r14,0x18(%rsp)
	movq  %r15,0x20(%rsp)
	movq  %rbx,0x28(%rsp)
	movq  %rbp,0x30(%rsp)
	movq  %rsp,%rax
	movq  %rdi,%rsp
	ldmxcsr (%rsp)
	fldcw   0x4(%rsp)
	movq  0x8(%rsp),%r12
	movq  0x10(%rsp),%r13
	movq  0x18(%rsp),%r14
	movq  0x20(%rsp),%r15
	movq  0x28(%rsp),%rbx
	movq  0x30(%rsp),%rbp
	leaq  0x38(%rsp),%rsp
	movq  %rsi,%rdx
	movq  %rax,%rdi
	jmp  *%r8
.size ontop_fcontext,.-ontop_fcontext
.section .note.GNU-stack,"",%progbits
)");

#else // F8FIBER_USE_ASM_SOURCE, define in your source compilation unit before including this file and then declare F8FIBER_ASM_SOURCE

/// The following macro(;) must appear in one compilation unit (not a header)
#define F8FIBER_ASM_SOURCE										\
asm(".text\n" 														\
".globl jump_fcontext,make_fcontext,ontop_fcontext\n"	\
".align 16\n" 														\
".type jump_fcontext,@function\n" 							\
"jump_fcontext:\n" 												\
"	leaq  -0x38(%rsp), %rsp\n" 								\
"	stmxcsr  (%rsp)\n" 											\
"	fnstcw   0x4(%rsp)\n" 										\
"	movq  %r12,0x8(%rsp)\n" 									\
"	movq  %r13,0x10(%rsp)\n" 									\
"	movq  %r14,0x18(%rsp)\n" 									\
"	movq  %r15,0x20(%rsp)\n" 									\
"	movq  %rbx,0x28(%rsp)\n" 									\
"	movq  %rbp,0x30(%rsp)\n" 									\
"	movq  %rsp,%rax\n" 											\
"	movq  %rdi,%rsp\n" 											\
"	movq  0x38(%rsp),%r8\n" 									\
"	ldmxcsr (%rsp)\n" 											\
"	fldcw   0x4(%rsp)\n" 										\
"	movq  0x8(%rsp),%r12\n" 									\
"	movq  0x10(%rsp),%r13\n" 									\
"	movq  0x18(%rsp),%r14\n" 									\
"	movq  0x20(%rsp),%r15\n" 									\
"	movq  0x28(%rsp),%rbx\n" 									\
"	movq  0x30(%rsp),%rbp\n" 									\
"	leaq  0x40(%rsp),%rsp\n" 									\
"	movq  %rsi,%rdx\n" 											\
"	movq  %rax,%rdi\n" 											\
"	jmp  *%r8\n" 													\
".size jump_fcontext,.-jump_fcontext\n" 					\
".type make_fcontext,@function\n" 							\
"make_fcontext:\n" 												\
"	movq  %rdi,%rax\n" 											\
"	andq  $-16,%rax\n" 											\
"	leaq  -0x40(%rax),%rax\n" 									\
"	movq  %rdx,0x28(%rax)\n" 									\
"	stmxcsr (%rax)\n" 											\
"	fnstcw  0x4(%rax)\n" 										\
"	leaq  trampoline(%rip),%rcx\n" 							\
"	movq  %rcx,0x38(%rax)\n" 									\
"	leaq  finish(%rip),%rcx\n" 								\
"	movq  %rcx,0x30(%rax)\n" 									\
"	ret\n" 															\
"trampoline:\n" 													\
"	push %rbp\n" 													\
"	jmp *%rbx\n" 													\
"finish:\n" 														\
"	xorq  %rdi,%rdi\n" 											\
"	call  _exit@PLT\n" 											\
"	hlt\n" 															\
".size make_fcontext,.-make_fcontext\n	"	 				\
".type ontop_fcontext,@function\n" 							\
"ontop_fcontext:\n" 												\
"	movq  %rdx,%r8\n" 											\
"	leaq  -0x38(%rsp),%rsp\n" 									\
"	stmxcsr (%rsp)\n" 											\
"	fnstcw  0x4(%rsp)\n" 										\
"	movq  %r12,0x8(%rsp)\n" 									\
"	movq  %r13,0x10(%rsp)\n" 									\
"	movq  %r14,0x18(%rsp)\n" 									\
"	movq  %r15,0x20(%rsp)\n" 									\
"	movq  %rbx,0x28(%rsp)\n" 									\
"	movq  %rbp,0x30(%rsp)\n" 									\
"	movq  %rsp,%rax\n" 											\
"	movq  %rdi,%rsp\n" 											\
"	ldmxcsr (%rsp)\n" 											\
"	fldcw   0x4(%rsp)\n" 										\
"	movq  0x8(%rsp),%r12\n" 									\
"	movq  0x10(%rsp),%r13\n" 									\
"	movq  0x18(%rsp),%r14\n" 									\
"	movq  0x20(%rsp),%r15\n" 									\
"	movq  0x28(%rsp),%rbx\n" 									\
"	movq  0x30(%rsp),%rbp\n" 									\
"	leaq  0x38(%rsp),%rsp\n" 									\
"	movq  %rsi,%rdx\n" 											\
"	movq  %rax,%rdi\n" 											\
"	jmp  *%r8\n" 													\
".size ontop_fcontext,.-ontop_fcontext\n" 				\
".section .note.GNU-stack,\"\",%progbits\n" 				\
)

#endif // F8FIBER_USE_ASM_SOURCE

//-----------------------------------------------------------------------------------------
// http://ericniebler.com/2013/08/07/universal-references-and-the-copy-constructo/
template<typename X, typename Y>
using disable_overload = typename std::enable_if<!std::is_base_of<X, typename std::decay<Y>::type>::value>::type;

struct forced_unwind
{
	fcontext_t fctx{};
	forced_unwind() = default;
	forced_unwind(fcontext_t fctx_) : fctx(fctx_) {}
};

inline fcontext_transfer_t fiber_unwind(fcontext_transfer_t t)
{
	throw forced_unwind(t.ctx);
	return {};
}

template<typename Rec>
fcontext_transfer_t fiber_exit(fcontext_transfer_t t) noexcept
{
	//std::cout << "fiber_exit\n";
	// destroy context stack
	static_cast<Rec *>(t.data)->deallocate();
	return {};
}

template<typename Rec>
void fiber_entry(fcontext_transfer_t t) noexcept
{
	// transfer control structure to the context-stack
	Rec *rec { static_cast<Rec *>(t.data) };
	try
	{
		t = jump_fcontext(t.ctx, nullptr); // jump back to `create_context()`
		t.ctx = rec->run(t.ctx); // start executing
	}
	catch (const forced_unwind& e)
	{
		t = { e.fctx, nullptr };
	}
	// destroy context-stack of `this`context on next context
	ontop_fcontext(t.ctx, rec, fiber_exit<Rec>);
}

template<typename Ctx, typename Fn>
fcontext_transfer_t fiber_ontop(fcontext_transfer_t t)
{
    auto p { *static_cast<Fn *>(t.data) };
    t.data = nullptr;
    // execute function, pass fiber via reference
    Ctx c { p(Ctx{t.ctx}) };
    return { std::exchange(c.fctx_, nullptr), nullptr };
}

template<typename Rec, typename StackAlloc, typename Fn>
fcontext_t create_fiber(StackAlloc&& salloc, Fn&& fn)
{
	auto sctx { salloc.allocate() };
	// reserve space for control structure
	void *storage { reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(sctx.sptr) - static_cast<uintptr_t>(sizeof(Rec)))
		& ~static_cast<uintptr_t>(0xff)) };
	// placment new for control structure on context stack
	Rec *record { new (storage) Rec { sctx, std::forward<StackAlloc>(salloc), std::forward<Fn>(fn) } };
	// 64byte gap between control structure and stack top, should be 16byte aligned
	void *top { reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(storage) - static_cast<uintptr_t>(64)) };
	void *bottom { reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(sctx.sptr) - static_cast<uintptr_t>(sctx.ssize)) };
	// create fast-context
	const std::size_t size { reinterpret_cast<uintptr_t>(top) - reinterpret_cast<uintptr_t>(bottom) };
	const fcontext_t fctx { make_fcontext(top, size, &fiber_entry<Rec>) };
	// transfer control structure to context-stack
	return jump_fcontext(fctx, record).ctx;
}

//-----------------------------------------------------------------------------------------
template<typename Ctx, typename StackAlloc, typename Fn>
class f8_fiber_record
{
	fcontext_stack_t _stack;
	typename std::decay< StackAlloc >::type _salloc;
	typename std::decay<Fn>::type _fn;

	static void destroy(f8_fiber_record *p) noexcept
	{
		typename std::decay<StackAlloc>::type salloc { std::move(p->_salloc) };
		auto stack { p->_stack };
		p->~f8_fiber_record();
		salloc.deallocate(stack);
	}

public:
	f8_fiber_record(fcontext_stack_t sctx, StackAlloc&& salloc, Fn&& fn) noexcept
		: _stack(sctx), _salloc(std::forward<StackAlloc>(salloc)), _fn(std::forward<Fn>(fn)) {}
	f8_fiber_record(const f8_fiber_record&) = delete;
	f8_fiber_record& operator=(const f8_fiber_record&) = delete;
	~f8_fiber_record() = default;

	void deallocate() noexcept { destroy(this); }

	fcontext_t run(fcontext_t fctx)
	{
		// invoke context-function
		Ctx c { std::invoke(_fn, std::move(Ctx{fctx})) };
		return std::exchange(c.fctx_, nullptr);
	}
};

//-----------------------------------------------------------------------------------------
class f8_fiber
{
	template<typename Ctx, typename StackAlloc, typename Fn>
	friend class f8_fiber_record;
	template<typename Ctx, typename Fn>
	friend fcontext_transfer_t fiber_ontop(fcontext_transfer_t);

	fcontext_t fctx_{ nullptr };

	f8_fiber(fcontext_t fctx) noexcept : fctx_{ fctx } {}

public:
	f8_fiber() noexcept = default;

	/*
	template<typename Fn, typename... Args , std::enable_if_t<!std::is_bind_expression_v<Fn>,int> = 0>
	f8_fiber(Fn&& fn, Args&&... args) : f8_fiber(std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...), 0) {} */

	template<typename Fn, typename = disable_overload<f8_fiber, Fn>>
	f8_fiber(Fn&& fn) : f8_fiber { std::allocator_arg, f8_protected_fixedsize_stack(), std::forward<Fn>(fn) } {}

	template<typename StackAlloc, typename Fn>
	f8_fiber(std::allocator_arg_t, StackAlloc&& salloc, Fn&& fn)
		: fctx_ { create_fiber<f8_fiber_record<f8_fiber, StackAlloc, Fn>>
			(std::forward<StackAlloc>(salloc), std::forward<Fn>(fn)) } {}

	virtual ~f8_fiber()
	{
		if (fctx_)
			ontop_fcontext(std::exchange(fctx_, nullptr), nullptr, fiber_unwind);
	}

	f8_fiber(f8_fiber&& other) noexcept { swap(other); }
	f8_fiber& operator=(f8_fiber&& other) noexcept
	{
		if (this != &other)
		{
			f8_fiber tmp { std::move(other) };
			swap(tmp);
		}
		return *this;
	}

	f8_fiber(const f8_fiber& other) noexcept = delete;
	f8_fiber& operator=(const f8_fiber& other) noexcept = delete;

	f8_fiber resume() && noexcept { return { jump_fcontext(std::exchange(fctx_, nullptr), nullptr).ctx }; }
	f8_fiber resume() & noexcept { return std::move(*this).resume(); }
	static void resume(f8_fiber&& what) noexcept
	{
		if (what)
			what = std::move(std::move(what).resume());
	}
	static void resume(f8_fiber& what) noexcept
	{
		if (what)
			what = std::move(what.resume());
	}

	template<typename Fn>
	f8_fiber resume_with(Fn&& fn) &&
	{
		auto p { std::forward<Fn>(fn) };
		return { ontop_fcontext(std::exchange(fctx_, nullptr), &p, fiber_ontop<f8_fiber, decltype(p)>).ctx };
	}

	explicit operator bool() const noexcept { return fctx_ != nullptr; }
	bool operator! () const noexcept { return fctx_ == nullptr; }
	bool operator< (const f8_fiber& other) const noexcept { return fctx_ < other.fctx_; }

	void swap(f8_fiber& other) noexcept { std::swap(fctx_, other.fctx_); }

	template<typename charT, class traitsT>
	friend std::basic_ostream<charT, traitsT>& operator<<(std::basic_ostream<charT, traitsT>& os, const f8_fiber& other)
	{
		if (other.fctx_)
			return os << other.fctx_;
		return os << "{not-a-context}";
	}

	class id
	{
		const fcontext_t impl_{ nullptr };

	public:
		id() = default;
		explicit id(const fcontext_t impl) noexcept : impl_{ impl } {}

#if __cplusplus >= 202002L
		auto operator<=>(const id& other) const noexcept { return impl_ <=> other.impl_; }
#else
		bool operator==(const id& other) const noexcept { return impl_ == other.impl_; }
		bool operator!=(const id& other) const noexcept { return impl_ != other.impl_; }
		bool operator<(const id& other) const noexcept { return impl_ < other.impl_; }
		bool operator>(const id& other) const noexcept { return other.impl_ < impl_; }
		bool operator<=(const id& other) const noexcept { return !(*this > other); }
		bool operator>=(const id& other) const noexcept { return !(*this < other); }
#endif

		template<typename charT, class traitsT>
		friend std::basic_ostream<charT, traitsT>& operator<<(std::basic_ostream<charT, traitsT>& os, const id& other)
		{
			if (other.impl_)
				return os << other.impl_;
			return os << "{not-valid}";
		}

		explicit operator bool() const noexcept { return impl_ != nullptr; }
		bool operator! () const noexcept { return impl_ == nullptr; }
	};

	friend f8_fiber::id;
	f8_fiber::id get_id() const noexcept { return id(fctx_); }
};

inline void swap(f8_fiber& l, f8_fiber& r) noexcept { l.swap(r); }

#define f8_yield(f) f8_fiber::resume(f)

//-----------------------------------------------------------------------------------------
} // namespace FIX8

#endif // FIX8_FIBER_HPP_

