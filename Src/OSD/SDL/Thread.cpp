/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011 Bart Trzynadlowski, Nik Henson
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * Thread.cpp
 *
 * SDL-based implementation of threading primitives.
 *
 * SDL3 renamed most of the synchronisation entry points (SDL_cond became
 * SDL_Condition, SDL_SemWait became SDL_WaitSemaphore, ...) and changed some
 * return types.  The thin inline adapters below let the class bodies below
 * stay identical for both backends without any per-call branching.
 */

#include "Thread.h"

#include "Supermodel.h"
#include "SDLIncludes.h"

#if defined(SUPERMODEL_PLATFORM_SDL3)

namespace
{
	using MutexHandle = SDL_Mutex;
	using SemaphoreHandle = SDL_Semaphore;
	using ConditionHandle = SDL_Condition;

	inline int LockMutex(MutexHandle *m) { SDL_LockMutex(m); return 0; }
	inline int UnlockMutex(MutexHandle *m) { SDL_UnlockMutex(m); return 0; }
	inline int WaitCond(ConditionHandle *c, MutexHandle *m) { SDL_WaitCondition(c, m); return 0; }
	inline int SignalCond(ConditionHandle *c) { SDL_SignalCondition(c); return 0; }
	inline int BroadcastCond(ConditionHandle *c) { SDL_BroadcastCondition(c); return 0; }
	inline void DestroyCond(ConditionHandle *c) { SDL_DestroyCondition(c); }
	inline UINT32 SemValue(SemaphoreHandle *s) { return (UINT32) SDL_GetSemaphoreValue(s); }
	inline int SemWait(SemaphoreHandle *s) { SDL_WaitSemaphore(s); return 0; }
	inline int SemPost(SemaphoreHandle *s) { SDL_SignalSemaphore(s); return 0; }
}

#else

namespace
{
	using MutexHandle = SDL_mutex;
	using SemaphoreHandle = SDL_sem;
	using ConditionHandle = SDL_cond;

	inline int LockMutex(MutexHandle *m) { return SDL_mutexP(m); }
	inline int UnlockMutex(MutexHandle *m) { return SDL_mutexV(m); }
	inline int WaitCond(ConditionHandle *c, MutexHandle *m) { return SDL_CondWait(c, m); }
	inline int SignalCond(ConditionHandle *c) { return SDL_CondSignal(c); }
	inline int BroadcastCond(ConditionHandle *c) { return SDL_CondBroadcast(c); }
	inline void DestroyCond(ConditionHandle *c) { SDL_DestroyCond(c); }
	inline UINT32 SemValue(SemaphoreHandle *s) { return SDL_SemValue(s); }
	inline int SemWait(SemaphoreHandle *s) { return SDL_SemWait(s); }
	inline int SemPost(SemaphoreHandle *s) { return SDL_SemPost(s); }
}

#endif

void CThread::Sleep(UINT32 ms)
{
	SDL_Delay(ms);
}

UINT32 CThread::GetTicks()
{
	return (UINT32) SDL_GetTicks();
}

CThread *CThread::CreateThread(const std::string &name, ThreadStart start, void *startParam)
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
	SDL_Thread *impl = SDL_CreateThread((SDL_ThreadFunction) start, name.c_str(), startParam);
#else
	SDL_Thread *impl = SDL_CreateThread(start, name.c_str(), startParam);
#endif
	if (impl == NULL)
		return NULL;
	return new CThread(name, impl);
}

CSemaphore *CThread::CreateSemaphore(UINT32 initVal)
{
	SemaphoreHandle *impl = SDL_CreateSemaphore(initVal);
	if (impl == NULL)
		return NULL;
	return new CSemaphore(impl);
}

CCondVar *CThread::CreateCondVar()
{
#if defined(SUPERMODEL_PLATFORM_SDL3)
	ConditionHandle *impl = SDL_CreateCondition();
#else
	ConditionHandle *impl = SDL_CreateCond();
#endif
	if (impl == NULL)
		return NULL;
	return new CCondVar(impl);
}

CMutex *CThread::CreateMutex()
{
	MutexHandle *impl = SDL_CreateMutex();
	if (impl == NULL)
		return NULL;
	return new CMutex(impl);
}

const char *CThread::GetLastError()
{
	return SDL_GetError();
}

CThread::CThread(const std::string &name, void *impl)
  : m_name(name),
    m_impl(impl)

{
	//
}

CThread::~CThread()
{
  // User should have called Wait() before thread object is destroyed
  if (nullptr != m_impl)
  {
    ErrorLog("Runaway thread error. A thread was not properly halted: %s\n", GetName().c_str());
  }
}

const std::string &CThread::GetName() const
{
  return m_name;
}

UINT32 CThread::GetId()
{
	return (UINT32) SDL_GetThreadID((SDL_Thread*)m_impl);
}

int CThread::Wait()
{
	int status;
	if (m_impl == NULL)
		return -1;
	SDL_WaitThread((SDL_Thread*)m_impl, &status);
	m_impl = NULL;
	return status;
}

CSemaphore::CSemaphore(void *impl) : m_impl(impl)
{
	//
}

CSemaphore::~CSemaphore()
{
	SDL_DestroySemaphore((SemaphoreHandle*)m_impl);
}

UINT32 CSemaphore::GetValue()
{
	return SemValue((SemaphoreHandle*)m_impl);
}

bool CSemaphore::Wait()
{
	return SemWait((SemaphoreHandle*)m_impl) == 0;
}

bool CSemaphore::Post()
{
	return SemPost((SemaphoreHandle*)m_impl) == 0;
}

CCondVar::CCondVar(void *impl) : m_impl(impl)
{
	//
}

CCondVar::~CCondVar()
{
	DestroyCond((ConditionHandle*)m_impl);
}

bool CCondVar::Wait(CMutex *mutex)
{
	return WaitCond((ConditionHandle*)m_impl, (MutexHandle*)mutex->m_impl) == 0;
}

bool CCondVar::Signal()
{
	return SignalCond((ConditionHandle*)m_impl) == 0;
}

bool CCondVar::SignalAll()
{
	return BroadcastCond((ConditionHandle*)m_impl) == 0;
}

CMutex::CMutex(void *impl) : m_impl(impl)
{
	//
}

CMutex::~CMutex()
{
	SDL_DestroyMutex((MutexHandle*)m_impl);
}

bool CMutex::Lock()
{
	return LockMutex((MutexHandle*)m_impl) == 0;
}

bool CMutex::Unlock()
{
	return UnlockMutex((MutexHandle*)m_impl) == 0;
}
