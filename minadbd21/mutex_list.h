/* the list of mutexes used by adb */
/* #ifndef __MUTEX_LIST_H
 * Do not use an include-guard. This file is included once to declare the locks
 * and once in win32 to actually do the runtime initialization.
 */
#ifndef ADB_MUTEX
#error ADB_MUTEX not defined when including this file
#endif
ADB_MUTEX(dns_lock)
ADB_MUTEX(socket_list_lock)
ADB_MUTEX(transport_lock)
#if ADB_HOST
ADB_MUTEX(local_transports_lock)
#endif
ADB_MUTEX(usb_lock)

// Sadly logging to /data/adb/adb-... is not thread safe.
//  After modifying adb.h::D() to count invocations:
//   DEBUG(jpa):0:Handling main()
//   DEBUG(jpa):1:[ usb_init - starting thread ]
// (Oopsies, no :2:, and matching message is also gone.)
//   DEBUG(jpa):3:[ usb_thread - opening device ]
//   DEBUG(jpa):4:jdwp control socket started (10)
ADB_MUTEX(D_lock)

#undef ADB_MUTEX
