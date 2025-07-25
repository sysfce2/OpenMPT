/*
 * sched_setscheduler.c
 *
 * Description:
 * POSIX thread functions that deal with thread scheduling.
 *
 * --------------------------------------------------------------------------
 *
 *      pthreads-win32 - POSIX Threads Library for Win32
 *      Copyright(C) 1998 John E. Bossom
 *      Copyright(C) 1999-2021 pthreads-win32 / pthreads4w contributors
 *
 *      Homepage1: http://sourceware.org/pthreads-win32/
 *      Homepage2: http://sourceforge.net/projects/pthreads4w/
 *
 *      The current list of contributors is contained
 *      in the file CONTRIBUTORS included with the source
 *      code distribution. The list can also be seen at the
 *      following World Wide Web location:
 *      http://sources.redhat.com/pthreads-win32/contributors.html
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library in the file COPYING.LIB;
 *      if not, write to the Free Software Foundation, Inc.,
 *      59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * --------------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "pthread.h"
#include "implement.h"
#include "sched.h"

int
sched_setscheduler (pid_t pid, int policy)
{
  /*
   * Win32 only has one policy which we call SCHED_OTHER.
   * However, we try to provide other valid side-effects
   * such as EPERM and ESRCH errors. Choosing to check
   * for a valid policy last allows us to get the most value out
   * of this function.
   */
  if (0 != pid)
    {
      pid_t selfPid = (pid_t) GetCurrentProcessId ();

      if (pid != selfPid)
	{
	  HANDLE h =
#if 0  /* OpenMPT */
	    OpenProcess (PROCESS_SET_INFORMATION, PTW32_FALSE, pid);
#else  /* OpenMPT */
	    /* <https://github.com/GerHobbelt/pthread-win32/issues/27> */  /* OpenMPT */
	    OpenProcess (PROCESS_SET_INFORMATION, PTW32_FALSE, (DWORD) pid);  /* OpenMPT */
#endif  /* OpenMPT */

	  if (NULL == h)
	    {
	      PTW32_SET_ERRNO((GetLastError () == (0xFF & ERROR_ACCESS_DENIED)) ? EPERM : ESRCH);
	      return -1;
	    }
	  else
	    CloseHandle(h);
	}
    }

  if (SCHED_OTHER != policy)
    {
      PTW32_SET_ERRNO(ENOSYS);
      return -1;
    }

  /*
   * Don't set anything because there is nothing to set.
   * Just return the current (the only possible) value.
   */
  return SCHED_OTHER;
}
