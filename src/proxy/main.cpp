/**
 * The seeks proxy is part of the SEEKS project
 * It is based on Privoxy (http://www.privoxy.org), developped
 * by the Privoxy team.
 *
 * Copyright (C) 2009 Emmanuel Benazera, juban@free.fr
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "seeks_proxy.h"
#include "errlog.h"
#include "miscutil.h"
#include "cgi.h"
#include "spsockets.h"

#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <signal.h>

#include <iostream>

using namespace sp;

/********************************************************************
 *
 * Function    :  main
 *
 * Description :  Load the config file and start the listen loop.
 *                This function is a lot more *sane* with the load_config'
 *                and listen_loop' functions; although it stills does
 *                a *little* too much for my taste.
 *
 * Parameters  :
 *          1  :  argc = Number of parameters (including $0).
 *          2  :  argv = Array of (char *)'s to the parameters.
 *
 * Returns     :  1 if : can't open config file, unrecognized directive,
 *                stats requested in multi-thread mode, can't open the
 *                log file, can't open the jar file, listen port is invalid,
 *                any load fails, and can't bind port.
 *
 *                Else main never returns, the process must be signaled
 *                to terminate execution.  Or, on Windows, use the
 *                "File", "Exit" menu option.
 *
 *********************************************************************/
#ifdef __MINGW32__
int real_main(int argc, const char *argv[])
#else
int main(int argc, const char *argv[])
#endif
{
  int argc_pos = 0;
  unsigned int random_seed;
#ifdef unix
  struct passwd *pw = NULL;
  struct group *grp = NULL;
  char *p;
  int do_chroot = 0;
  char *pre_chroot_nslookup_to_load_resolver = NULL;
#endif

  seeks_proxy::_Argc = argc;
  seeks_proxy::_Argv = argv;

  seeks_proxy::_configfile =
#if !defined(_WIN32)
    "config"
#else
    "config.txt"
#endif
    ;

  /* Prepare mutexes if supported and necessary. */
  seeks_proxy::initialize_mutexes();

  /* Enable logging until further notice. */
  errlog::init_log_module();

  /*
   * Parse the command line arguments
   */
  while (++argc_pos < argc)
    {
#ifdef _WIN32
      /* Check to see if the service must be installed or uninstalled */
      if (strncmp(argv[argc_pos], "--install", 9) == 0)
        {
          const char *pName = argv[argc_pos] + 9;
          if (*pName == ':')
            pName++;
          exit( (install_service(pName)) ? 0 : 1 );
        }
      else if (strncmp(argv[argc_pos], "--uninstall", + 11) == 0)
        {
          const char *pName = argv[argc_pos] + 11;
          if (*pName == ':')
            pName++;
          exit((uninstall_service(pName)) ? 0 : 1);
        }
      else if (strcmp(argv[argc_pos], "--service" ) == 0)
        {
          bRunAsService = TRUE;
          w32_set_service_cwd();
          atexit(w32_service_exit_notify);
        }
      else
#endif /* defined(_WIN32) */
#if !defined(_WIN32) || defined(_WIN_CONSOLE)
        if (strcmp(argv[argc_pos], "--help") == 0)
          {
            seeks_proxy::usage(argv[0]);
          }
        else if (strcmp(argv[argc_pos], "--version") == 0)
          {
            printf("Seeks version " VERSION " (" HOME_PAGE_URL ")\n");
            exit(0);
          }

# if defined(unix)
        else if (strcmp(argv[argc_pos], "--no-daemon" ) == 0)
          {
            errlog::set_debug_level(LOG_LEVEL_FATAL | LOG_LEVEL_ERROR | LOG_LEVEL_INFO);
            seeks_proxy::_no_daemon = 1;
          }
        else if (strcmp(argv[argc_pos], "--pidfile" ) == 0)
          {
            if (++argc_pos == argc) seeks_proxy::usage(argv[0]);
            seeks_proxy::_pidfile = strdup(argv[argc_pos]);
          }
        else if (strcmp(argv[argc_pos], "--user" ) == 0)
          {
            if (++argc_pos == argc) seeks_proxy::usage(argv[argc_pos]);

            if ((NULL != (p = strchr((char*)argv[argc_pos], '.'))) && *(p + 1) != '0')
              {
                *p++ = '\0';
                if (NULL == (grp = getgrnam(p)))
                  {
                    errlog::log_error(LOG_LEVEL_FATAL, "Group %s not found.", p);
                  }
              }
            if (NULL == (pw = getpwnam(argv[argc_pos])))
              {
                errlog::log_error(LOG_LEVEL_FATAL, "User %s not found.", argv[argc_pos]);
              }
            if (p != NULL) *--p = '\0';
          }
        else if (strcmp(argv[argc_pos], "--pre-chroot-nslookup" ) == 0)
          {
            if (++argc_pos == argc) seeks_proxy::usage(argv[0]);
            pre_chroot_nslookup_to_load_resolver = strdup(argv[argc_pos]);
          }
        else if (strcmp(argv[argc_pos], "--chroot" ) == 0)
          {
            do_chroot = 1;
          }
# endif /* defined(unix) */
        else if (argc_pos + 1 != argc)
          {
            /*
             * This is neither the last command line
             * option, nor was it recognized before,
             * therefore it must be invalid.
             */
            seeks_proxy::usage(argv[0]);
          }
        else
#endif /* defined(_WIN32) && !defined(_WIN_CONSOLE) */
          {
            seeks_proxy::_configfile = argv[argc_pos];
          }
    } /* -END- while (more arguments) */

  errlog::show_version(seeks_proxy::_Argv[0]);

#if defined(unix)
  if (*seeks_proxy::_configfile.c_str() != '/' )
    {
      char cwd[BUFFER_SIZE];
      char *abs_file;
      size_t abs_file_size;

      /* make config-filename absolute here */
      if (NULL == getcwd(cwd, sizeof(cwd)))
        {
          perror("failed to get current working directory");
          exit( 1 );
        }

      /* XXX: why + 5? */
      abs_file_size = strlen(cwd) + seeks_proxy::_configfile.length() + 5;
      seeks_proxy::_basedir = strdup(cwd);

      if (NULL == seeks_proxy::_basedir ||
          NULL == (abs_file = (char*) zalloc(abs_file_size)))
        {
          perror("malloc failed");
          exit( 1 );
        }

      strlcpy(abs_file, seeks_proxy::_basedir, abs_file_size);
      strlcat(abs_file, "/", abs_file_size );
      strlcat(abs_file, seeks_proxy::_configfile.c_str(), abs_file_size);
      seeks_proxy::_configfile = std::string(abs_file);
    }
#endif /* defined unix */

  //seeks_proxy::_files._next = NULL;
  seeks_proxy::_clients._next = NULL;

#if defined(_WIN32)
  InitWin32();
#endif

  random_seed = (unsigned int)time(NULL);
#ifdef HAVE_RANDOM
  srandom(random_seed);
#else
  srand(random_seed);
#endif /* ifdef HAVE_RANDOM */

  /**
   * Unix signal handling
   *
   * Catch the abort, interrupt and terminate signals for a graceful exit
   * Catch the hangup signal so the errlog can be reopened.
   * Ignore the broken pipe signals (FIXME: Why?)
   */
#if !defined(_WIN32)
  {
    int idx;
    const int catched_signals[] = { SIGTERM, SIGINT, SIGHUP, 0 };
    const int ignored_signals[] = { SIGPIPE, 0 };

    for (idx = 0; catched_signals[idx] != 0; idx++)
      {
# ifdef sun /* FIXME: Is it safe to check for HAVE_SIGSET instead? */
        if (sigset(catched_signals[idx], &seek_proxy::sig_handler) == SIG_ERR)
# else
        if (signal(catched_signals[idx], &seeks_proxy::sig_handler) == SIG_ERR)
# endif /* ifdef sun */
          {
            errlog::log_error(LOG_LEVEL_FATAL, "Can't set signal-handler for signal %d: %E", catched_signals[idx]);
          }
      }

    for (idx = 0; ignored_signals[idx] != 0; idx++)
      {
        if (signal(ignored_signals[idx], SIG_IGN) == SIG_ERR)
          {
            errlog::log_error(LOG_LEVEL_FATAL, "Can't set ignore-handler for signal %d: %E", ignored_signals[idx]);
          }
      }
  }
#else /* ifdef _WIN32 */
# ifdef _WIN_CONSOLE
  /*
   * We *are* in a windows console app.
   * Print a verbose messages about FAQ's and such
   */
  printf("%s", win32_blurb);
# endif /* def _WIN_CONSOLE */
#endif /* def _WIN32 */

  /* Initialize the CGI subsystem */
  cgi::cgi_init_error_messages();

  /*
   *
   * If runnig on unix and without the --nodaemon
   * option, become a daemon. I.e. fork, detach
   * from tty and get process group leadership
   */
#if defined(unix)
  {
    pid_t pid = 0;
# if 0
    int   fd;
# endif

    if (!seeks_proxy::_no_daemon)
      {
        pid  = fork();

        if ( pid < 0 ) /* error */
          {
            perror("fork");
            exit( 3 );
          }
        else if ( pid != 0 ) /* parent */
          {
            int status;
            pid_t wpid;
            /*
             * must check for errors
             * child died due to missing files aso
             */
            sleep( 1 );
            wpid = waitpid( pid, &status, WNOHANG );
            if ( wpid != 0 )
              {
                exit( 1 );
              }
            exit( 0 );
          }
        /* child */
# if 1
        /* Should be more portable, but not as well tested */
        setsid();
# else /* !1 */
#  ifdef __FreeBSD__
        setpgrp(0,0);
#  else /* ndef __FreeBSD__ */
        setpgrp();
#  endif /* ndef __FreeBSD__ */
        fd = open("/dev/tty", O_RDONLY);
        if ( fd )
          {
            /* no error check here */
            ioctl( fd, TIOCNOTTY,0 );
            close ( fd );
          }
# endif /* 1 */
        /*
         * stderr (fd 2) will be closed later on,
         * when the config file has been parsed.
         */
        close( 0 );
        close( 1 );
        chdir("/");
      }
    /* -END- if (!no_daemon) */

    /*
     * As soon as we have written the PID file, we can switch
     * to the user and group ID indicated by the --user option
     */
    miscutil::write_pid_file();

    if (NULL != pw)
      {
        if (setgid((NULL != grp) ? grp->gr_gid : pw->pw_gid))
          {
            errlog::log_error(LOG_LEVEL_FATAL, "Cannot setgid(): Insufficient permissions.");
          }
        if (NULL != grp)
          {
            if (setgroups(1, &grp->gr_gid))
              {
                errlog::log_error(LOG_LEVEL_FATAL, "setgroups() failed: %E");
              }
          }
        else if (initgroups(pw->pw_name, pw->pw_gid))
          {
            errlog::log_error(LOG_LEVEL_FATAL, "initgroups() failed: %E");
          }
        if (do_chroot)
          {
            if (!pw->pw_dir)
              {
                errlog::log_error(LOG_LEVEL_FATAL, "Home directory for %s undefined", pw->pw_name);
              }
            /* Read the time zone file from /etc before doing chroot. */
            tzset();
            if (NULL != pre_chroot_nslookup_to_load_resolver
                && '\0' != pre_chroot_nslookup_to_load_resolver[0])
              {
                /* Initialize resolver library. */ // TODO.
                (void) spsockets::resolve_hostname_to_ip(pre_chroot_nslookup_to_load_resolver);
              }
            if (chroot(pw->pw_dir) < 0)
              {
                errlog::log_error(LOG_LEVEL_FATAL, "Cannot chroot to %s", pw->pw_dir);
              }
            if (chdir ("/"))
              {
                errlog::log_error(LOG_LEVEL_FATAL, "Cannot chdir /");
              }
          }
        if (setuid(pw->pw_uid))
          {
            errlog::log_error(LOG_LEVEL_FATAL, "Cannot setuid(): Insufficient permissions.");
          }
        if (do_chroot)
          {
            char putenv_dummy[64];

            strlcpy(putenv_dummy, "HOME=/", sizeof(putenv_dummy));
            if (putenv(putenv_dummy) != 0)
              {
                errlog::log_error(LOG_LEVEL_FATAL, "Cannot putenv(): HOME");
              }
            snprintf(putenv_dummy, sizeof(putenv_dummy), "USER=%s", pw->pw_name);
            if (putenv(putenv_dummy) != 0)
              {
                errlog::log_error(LOG_LEVEL_FATAL, "Cannot putenv(): USER");
              }
          }
      }
    else if (do_chroot)
      {
        errlog::log_error(LOG_LEVEL_FATAL, "Cannot chroot without --user argument.");
      }
  }
#endif /* defined unix */

#ifdef _WIN32
  /* This will be FALSE unless the command line specified --service */
  if (bRunAsService)
    {
      /* Yup, so now we must attempt to establish a connection
       * with the service dispatcher. This will only work if this
       * process was launched by the service control manager to
       * actually run as a service. If this isn't the case, i've
       * known it take around 30 seconds or so for the call to return.
       */

      /* The StartServiceCtrlDispatcher won't return until the service is stopping */
      if (w32_start_service_ctrl_dispatcher(w32ServiceDispatchTable))
        {
          /* Service has run, and at this point is now being stopped, so just return */
          return 0;
        }

# ifdef _WIN_CONSOLE
      printf("Warning: Failed to connect to Service Control Dispatcher\nwhen starting as a service!\n");
# endif
      /* An error occurred. Usually it's because --service was wrongly specified
       * and we were unable to connect to the Service Control Dispatcher because
       * it wasn't expecting us and is therefore not listening.
       *
       * For now, just continue below to call the listen_loop function.
       */
    }
#endif /* def _WIN32 */

  seeks_proxy::listen_loop();

  /* NOTREACHED */
  return(-1);
}
