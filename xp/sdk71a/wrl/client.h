/*
 * wrl/client.h - minimal Microsoft::WRL::ComPtr backfill for the Windows 7 SDK
 * (7.1A), which predates the WRL. The full WRL client.h pulls in WinRT headers
 * (roapi.h, winrt.h) absent on the XP SDK, so this provides just the COM smart
 * pointer the app uses (no WinRT/agile features). Sufficient for classic COM
 * (IShellLink, IPersistFile, IPropertyStore, ...).
 */
#ifndef _XP_WRL_CLIENT_H_
#define _XP_WRL_CLIENT_H_

#ifdef _MSC_VER
#pragma once
#endif

#include <unknwn.h>
#include <memory> // std::addressof - operator& below is taken by the COM idiom.

namespace Microsoft {
namespace WRL {

template <typename T>
class ComPtr {
public:
	typedef T InterfaceType;

	ComPtr() noexcept : ptr_(nullptr) {}
	ComPtr(decltype(nullptr)) noexcept : ptr_(nullptr) {}
	ComPtr(T *other) noexcept : ptr_(other) { InternalAddRef(); }
	ComPtr(const ComPtr &other) noexcept : ptr_(other.ptr_) { InternalAddRef(); }
	template <typename U>
	ComPtr(const ComPtr<U> &other) noexcept : ptr_(other.Get()) { InternalAddRef(); }
	ComPtr(ComPtr &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
	~ComPtr() noexcept { InternalRelease(); }

	ComPtr &operator=(decltype(nullptr)) noexcept { InternalRelease(); return *this; }
	ComPtr &operator=(T *other) noexcept {
		if (ptr_ != other) {
			T *old = ptr_;
			ptr_ = other;
			InternalAddRef();
			if (old) old->Release();
		}
		return *this;
	}
	ComPtr &operator=(const ComPtr &other) noexcept { return operator=(other.ptr_); }
	ComPtr &operator=(ComPtr &&other) noexcept {
		// std::addressof, not &other: operator& below is the COM out-parameter
		// idiom and yields T**, so the plain self-check does not even compile.
		if (this != std::addressof(other)) {
			InternalRelease();
			ptr_ = other.ptr_;
			other.ptr_ = nullptr;
		}
		return *this;
	}

	T *Get() const noexcept { return ptr_; }
	T *operator->() const noexcept { return ptr_; }
	explicit operator bool() const noexcept { return ptr_ != nullptr; }

	T **GetAddressOf() noexcept { return &ptr_; }
	T **ReleaseAndGetAddressOf() noexcept { InternalRelease(); return &ptr_; }
	T **operator&() noexcept { return ReleaseAndGetAddressOf(); }

	T *Detach() noexcept { T *p = ptr_; ptr_ = nullptr; return p; }
	void Reset() noexcept { InternalRelease(); }
	void Attach(T *other) noexcept { InternalRelease(); ptr_ = other; }

	template <typename U>
	HRESULT As(ComPtr<U> *p) const noexcept {
		return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void **>(p->ReleaseAndGetAddressOf()));
	}
	template <typename U>
	HRESULT As(U **p) const noexcept {
		return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void **>(p));
	}
	HRESULT CopyTo(REFIID riid, void **ptr) const noexcept { return ptr_->QueryInterface(riid, ptr); }

private:
	void InternalAddRef() const noexcept { if (ptr_) ptr_->AddRef(); }
	void InternalRelease() noexcept { T *old = ptr_; if (old) { ptr_ = nullptr; old->Release(); } }

	T *ptr_;
};

namespace Details {

// Minimal ComPtrRef so headers declaring Details::ComPtrRef<T> parameters
// (e.g. WinRT GetActivationFactory wrappers in lib_base/base_windows_wrl.h)
// PARSE on the 7.1A SDK. WinRT activation is a no-op on XP, so this is never
// instantiated -- it only needs to be a declared type.
template <typename T>
class ComPtrRef {
public:
	ComPtrRef(T **ptr) noexcept : ptr_(ptr) {}
	T **ReleaseAndGetAddressOf() const noexcept { return ptr_; }
	operator T **() const noexcept { return ptr_; }

private:
	T **ptr_;
};

} // namespace Details

} // namespace WRL
} // namespace Microsoft

#endif // _XP_WRL_CLIENT_H_
