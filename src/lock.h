/*
This file is a part of the NV Speech Player project.
URL: https://bitbucket.org/nvaccess/speechplayer
Copyright 2014 NV Access Limited.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2.0, as published by
the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

#ifndef NVDAHELPER_LOCK_H
#define NVDAHELPER_LOCK_H

#include <cassert>

// This project originally used Win32 CRITICAL_SECTION + Interlocked*
// for a small re-entrant lock + refcount helper.
//
// For Linux/Android builds we provide an equivalent implementation based on
// std::recursive_mutex and std::atomic. This keeps the public DSP/Frontend ABI
// unchanged (these classes are internal implementation details).

#if defined(_WIN32) || defined(__CYGWIN__)

// Prevent windows.h from defining min/max macros (they break std::min/std::max).
#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <windows.h>

/**
 * A class that provides a locking mechonism on objects.
 * The lock is reeentrant for the same thread.
 */
class LockableObject {
	private:
	CRITICAL_SECTION _cs;

	public:

	LockableObject() {
		InitializeCriticalSection(&_cs);
	}

	virtual ~LockableObject() {
		DeleteCriticalSection(&_cs);
	}

/**
 * Acquires access (possibly waighting until its free).
 */
	void acquire() {
		EnterCriticalSection(&_cs);
	}

/**
 * Releases exclusive access of the object.
 */
	void release() {
		LeaveCriticalSection(&_cs);
	}

};

/**
 * A class providing both exclusive locking, and reference counting with auto-deletion.
 * Do not use this in multiple inheritence.
 */
class LockableAutoFreeObject: private LockableObject {
	private:
	volatile long _refCount;

	protected:

	long incRef() {
		return InterlockedIncrement(&_refCount);
	}

	long decRef() {
		long refCount=InterlockedDecrement(&_refCount);
		if(refCount==0) {
			delete this;
		}
		assert(refCount>=0);
		return refCount;
	}

	public:

	LockableAutoFreeObject(): _refCount(1) {
	}

/**
 * Increases the reference count and acquires exclusive access.
 */
	void acquire() {
		incRef();
		LockableObject::acquire();
	}

	void release() {
		LockableObject::release();
		decRef();
	}

/**
 * Deletes this object if no one has acquired it, or indicates that it should be deleted once it has been released.
 */
	void requestDelete() {
		decRef();
	}

};

#else  // Non-Windows (Linux / Android / etc.)

#include <atomic>
#include <mutex>

/**
 * A small re-entrant lock.
 *
 * CRITICAL_SECTION is re-entrant for the same thread, so on POSIX we use
 * std::recursive_mutex to preserve behaviour.
 */
class LockableObject {
	private:
	std::recursive_mutex _mtx;

	public:
	LockableObject() = default;
	virtual ~LockableObject() = default;

	void acquire() {
		_mtx.lock();
	}

	void release() {
		_mtx.unlock();
	}

};

/**
 * Exclusive locking + reference counting with auto-deletion.
 * Do not use this in multiple inheritance.
 */
class LockableAutoFreeObject: private LockableObject {
	private:
	std::atomic<long> _refCount;

	protected:
	long incRef() {
		// seq_cst matches the "strong" behaviour of Interlocked*.
		return ++_refCount;
	}

	long decRef() {
		long refCount = --_refCount;
		if(refCount == 0) {
			delete this;
		}
		assert(refCount >= 0);
		return refCount;
	}

	public:
	LockableAutoFreeObject(): _refCount(1) {}

	void acquire() {
		incRef();
		LockableObject::acquire();
	}

	void release() {
		LockableObject::release();
		decRef();
	}

	void requestDelete() {
		decRef();
	}

};

#endif

#endif
