/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * cfgfile.c
 *  - reading of configuration file
 * --
 * @(#) $Id: cfgfile.c,v 1.23 2000/10/13 13:35:42 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "irc_net.h"
#include "cfgfile.h"

/* forward declaration */
static int _cfg_read_bool(char **, int *);
static int _cfg_read_numeric(char **, long *);
static int _cfg_read_string(char **, char **);

/* Whitespace */
#define WS " \t\r\n"

/* Quick and easy "Unmatched Quote" define */
#define UNMATCHED_QUOTE { error("Unmatched quote for key '%s' " \
                                "at line %ld of %s", key, line, \
                                filename); valid = 0; break; }

/* Read a config file */
int cfg_read(const char *filename, char **listen_port,
             struct globalvars *globals) {
  struct ircconnclass defaults, *def, *class;
  int valid;
  long line;
  FILE *fd;

  def = &defaults;
  memset(globals, 0, sizeof(struct globalvars));
  memset(def, 0, sizeof(struct ircconnclass));
  class = 0;
  line = 0;
  valid = 1;
  fd = fopen(filename, "r");
  if (!fd)
    return -1;

  /* Initialise globals */
  globals->client_timeout = DEFAULT_CLIENT_TIMEOUT;
  globals->connect_timeout = DEFAULT_CONNECT_TIMEOUT;
  globals->dns_timeout = DEFAULT_DNS_TIMEOUT;

  /* Initialise using defaults */
  def->server_port = x_strdup(DEFAULT_SERVER_PORT ? DEFAULT_SERVER_PORT : "0");
  def->server_retry = DEFAULT_SERVER_RETRY;
  def->server_dnsretry = DEFAULT_SERVER_DNSRETRY;
  def->server_maxattempts = DEFAULT_SERVER_MAXATTEMPTS;
  def->server_maxinitattempts = DEFAULT_SERVER_MAXINITATTEMPTS;
  def->server_pingtimeout = DEFAULT_SERVER_PINGTIMEOUT;
  def->server_autoconnect = DEFAULT_SERVER_AUTOCONNECT;
  def->channel_rejoin = DEFAULT_CHANNEL_REJOIN;
  def->channel_leave_on_detach = DEFAULT_CHANNEL_LEAVE_ON_DETACH;
  def->channel_rejoin_on_attach = DEFAULT_CHANNEL_REJOIN_ON_ATTACH;
  def->idle_maxtime = DEFAULT_IDLE_MAXTIME;
  def->disconnect_existing = DEFAULT_DISCONNECT_EXISTING;
  def->disconnect_on_detach = DEFAULT_DISCONNECT_ON_DETACH;
  def->drop_modes = (DEFAULT_DROP_MODES ? x_strdup(DEFAULT_DROP_MODES) : 0);
  def->refuse_modes = (DEFAULT_REFUSE_MODES
                       ? x_strdup(DEFAULT_REFUSE_MODES) : 0);
  def->local_address = (DEFAULT_LOCAL_ADDRESS
                        ? x_strdup(DEFAULT_LOCAL_ADDRESS) : 0);
  def->away_message = (DEFAULT_AWAY_MESSAGE
                       ? x_strdup(DEFAULT_AWAY_MESSAGE) : 0);
  def->quit_message = (DEFAULT_QUIT_MESSAGE
                       ? x_strdup(DEFAULT_QUIT_MESSAGE) : 0);
  def->attach_message = (DEFAULT_ATTACH_MESSAGE
                         ? x_strdup(DEFAULT_ATTACH_MESSAGE) : 0);
  def->detach_message = (DEFAULT_DETACH_MESSAGE
                         ? x_strdup(DEFAULT_DETACH_MESSAGE) : 0);
  def->chan_log_enabled = DEFAULT_CHAN_LOG_ENABLED;
  def->chan_log_dir = (DEFAULT_CHAN_LOG_DIR
                       ? x_strdup(DEFAULT_CHAN_LOG_DIR) : 0);
  def->chan_log_program = (DEFAULT_CHAN_LOG_PROGRAM
                           ? x_strdup(DEFAULT_CHAN_LOG_PROGRAM) : 0);
  def->chan_log_always = DEFAULT_CHAN_LOG_ALWAYS;
  def->chan_log_timestamp = DEFAULT_CHAN_LOG_TIMESTAMP;
  def->chan_log_maxsize = DEFAULT_CHAN_LOG_MAXSIZE;
  def->chan_log_recall = DEFAULT_CHAN_LOG_RECALL;
  def->other_log_enabled = DEFAULT_OTHER_LOG_ENABLED;
  def->other_log_dir = (DEFAULT_OTHER_LOG_DIR
                        ? x_strdup(DEFAULT_OTHER_LOG_DIR) : 0);
  def->other_log_program = (DEFAULT_OTHER_LOG_PROGRAM
                            ? x_strdup(DEFAULT_OTHER_LOG_PROGRAM) : 0);
  def->other_log_always = DEFAULT_OTHER_LOG_ALWAYS;
  def->other_log_timestamp = DEFAULT_OTHER_LOG_TIMESTAMP;
  def->other_log_maxsize = DEFAULT_OTHER_LOG_MAXSIZE;
  def->other_log_recall = DEFAULT_OTHER_LOG_RECALL;
  def->motd_logo = DEFAULT_MOTD_LOGO;
  def->motd_file = (DEFAULT_MOTD_FILE ? x_strdup(DEFAULT_MOTD_FILE) : 0);
  def->motd_stats = DEFAULT_MOTD_STATS;
  def->allow_persist = DEFAULT_ALLOW_PERSIST;
  def->allow_jump = DEFAULT_ALLOW_JUMP;
  def->allow_jump_new = DEFAULT_ALLOW_JUMP_NEW;
  def->allow_host = DEFAULT_ALLOW_HOST;
  def->allow_die = DEFAULT_ALLOW_DIE;

  while (valid) {
    char buff[512], *buf;

    if (!fgets(buff, 512, fd))
      break;
    line++;

    buf = buff;
    while ((buf < (buff + 512)) && strlen(buf)) {
      char *key;

      /* Skip whitespace, and ignore lines that are comments */
      buf += strspn(buf, WS);
      if (*buf == '#')
        break;

      /* Find the end of the key, and if there isn't one, exit */
      key = buf;
      buf += strcspn(buf, WS);
      if (!strlen(key))
        break;

      /* If there isn't a newline, then we could reach the end of the
         buffer - so be a bit careful and ensure we don't skip past it */
      if (*buf) {
        *(buf++) = 0;

        /* Close brace is only allowed when class is defined
           (we check here, because its a special case and has no value) */
        if (!strcmp(key, "}") && !class) {
          error("Close brace without open at line %ld of %s", line, filename);
          valid = 0;
          break;
        }

        buf += strspn(buf, WS);
      }

      /* If we reached the end of the buffer, or a comment, that means
         this key has no value.  Unless the key is '}' then thats bad */
      if ((!*buf || (*buf == '#')) && strcmp(key, "}")) {
        error("Missing value for key '%s' at line %ld of %s",
              key, line, filename);
        valid = 0;
        break;
      }

      /* Handle the keys */
      if (!class && !strcasecmp(key, "listen_port")) {
        /* listen_port 57000
           listen_port "dircproxy"    # From /etc/services
         
           ( cannot go in a connection {} ) */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        /* Make sure the silly programmer supplied the pointer! */
        if (listen_port) {
          free(*listen_port);
          *listen_port = str;
        }

      } else if (!class && !strcasecmp(key, "client_timeout")) {
        /* client_timeout 60 */
        _cfg_read_numeric(&buf, &globals->client_timeout);

      } else if (!class && !strcasecmp(key, "connect_timeout")) {
        /* connect_timeout 60 */
        _cfg_read_numeric(&buf, &globals->connect_timeout);

      } else if (!class && !strcasecmp(key, "dns_timeout")) {
        /* dns_timeout 60 */
        _cfg_read_numeric(&buf, &globals->dns_timeout);

      } else if (!strcasecmp(key, "server_port")) {
        /* server_port 6667
           server_port "irc"    # From /etc/services */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free((class ? class : def)->server_port);
        (class ? class : def)->server_port = str;

      } else if (!strcasecmp(key, "server_retry")) {
        /* server_retry 15 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_retry);

      } else if (!strcasecmp(key, "server_dnsretry")) {
        /* server_dnsretry 15 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_dnsretry);

      } else if (!strcasecmp(key, "server_maxattempts")) {
        /* server_maxattempts 0 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_maxattempts);

      } else if (!strcasecmp(key, "server_maxinitattempts")) {
        /* server_maxinitattempts 5 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_maxinitattempts);
        
      } else if (!strcasecmp(key, "server_pingtimeout")) {
        /* server_pingtimeout 600 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_pingtimeout);

      } else if (!strcasecmp(key, "server_autoconnect")) {
        /* server_autoconnect yes
           server_autoconnect no */
        _cfg_read_bool(&buf, &(class ? class : def)->server_autoconnect);

      } else if (!strcasecmp(key, "channel_rejoin")) {
        /* channel_rejoin 5 */
        _cfg_read_numeric(&buf, &(class ? class : def)->channel_rejoin);

      } else if (!strcasecmp(key, "channel_leave_on_detach")) {
        /* channel_leave_on_detach yes
           channel_leave_on_detach no */
        _cfg_read_bool(&buf, &(class ? class : def)->channel_leave_on_detach);

      } else if (!strcasecmp(key, "channel_rejoin_on_attach")) {
        /* channel_rejoin_on_attach yes
           channel_rejoin_on_attach no */
        _cfg_read_bool(&buf, &(class ? class : def)->channel_rejoin_on_attach);

      } else if (!strcasecmp(key, "idle_maxtime")) {
        /* idle_maxtime 120 */
        _cfg_read_numeric(&buf, &(class ? class : def)->idle_maxtime);
 
      } else if (!strcasecmp(key, "disconnect_existing_user")) {
        /* disconnect_existing_user yes
           disconnect_existing_user no */
        _cfg_read_bool(&buf, &(class ? class : def)->disconnect_existing);

      } else if (!strcasecmp(key, "disconnect_on_detach")) {
        /* disconnect_on_detach yes
           disconnect_on_detach no */
        _cfg_read_bool(&buf, &(class ? class : def)->disconnect_on_detach);

      } else if (!strcasecmp(key, "drop_modes")) {
        /* drop_modes "ow"
           drop_modes "" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        while ((*str == '+') || (*str == '-')) {
          char *tmp;
          
          tmp = str;
          str = x_strdup(tmp + 1);
          free(tmp);
        }

        if (!strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->drop_modes);
        (class ? class : def)->drop_modes = str;

      } else if (!strcasecmp(key, "refuse_modes")) {
        /* refuse_modes "r"
           refuse_modes "" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        while ((*str == '+') || (*str == '-')) {
          char *tmp;
          
          tmp = str;
          str = x_strdup(tmp + 1);
          free(tmp);
        }

        if (!strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->refuse_modes);
        (class ? class : def)->refuse_modes = str;

      } else if (!strcasecmp(key, "local_address")) {
        /* local_address none
           local_address ""    # same as none
           local_address "i.am.a.virtual.host.com" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->local_address);
        (class ? class : def)->local_address = str;

      } else if (!strcasecmp(key, "away_message")) {
        /* away_message none
           away_message ""    # same as none
           away_message "Not available, messages are logged" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->away_message);
        (class ? class : def)->away_message = str;

      } else if (!strcasecmp(key, "quit_message")) {
        /* quit_message none
           quit_message ""    # same as none
           quit_message "Gotta restart this thing" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->quit_message);
        (class ? class : def)->quit_message = str;

      } else if (!strcasecmp(key, "attach_message")) {
        /* attach_message none
           attach_message ""    # same as none
           attach_message "I'm back!"
           attach_message "/me returns" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->attach_message);
        (class ? class : def)->attach_message = str;

      } else if (!strcasecmp(key, "detach_message")) {
        /* detach_message none
           detach_message ""    # same as none
           detach_message "I'm gone!"
           detach_message "/me vanishes" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->detach_message);
        (class ? class : def)->detach_message = str;

      } else if (!strcasecmp(key, "chan_log_enabled")) {
        /* chan_log_enabled yes
           chan_log_disabled no */
        _cfg_read_bool(&buf, &(class ? class : def)->chan_log_enabled);

      } else if (!strcasecmp(key, "chan_log_dir")) {
        /* chan_log_dir none
           chan_log_dir ""    # same as none
           chan_log_dir "/log"
           chan_log_dir "~/logs" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->chan_log_dir);
        (class ? class : def)->chan_log_dir = str;

      } else if (!strcasecmp(key, "chan_log_program")) {
        /* chan_log_program none
           chan_log_program ""    # same as none
           chan_log_program "/logprog"
           chan_log_program "~/logprog" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->chan_log_program);
        (class ? class : def)->chan_log_program = str;

      } else if (!strcasecmp(key, "chan_log_always")) {
        /* chan_log_always yes
           chan_log_always no */
        _cfg_read_bool(&buf, &(class ? class : def)->chan_log_always);

      } else if (!strcasecmp(key, "chan_log_timestamp")) {
        /* chan_log_timestamp yes
           chan_log_timestamp no */
        _cfg_read_bool(&buf, &(class ? class : def)->chan_log_timestamp);

      } else if (!strcasecmp(key, "chan_log_maxsize")) {
        /* chan_log_maxsize 128
           chan_log_maxsize 0 */
        _cfg_read_numeric(&buf, &(class ? class : def)->chan_log_maxsize);

      } else if (!strcasecmp(key, "chan_log_recall")) {
        /* chan_log_recall 128
           chan_log_recall 0
           chan_log_recall -1 */
        _cfg_read_numeric(&buf, &(class ? class : def)->chan_log_recall);

      } else if (!strcasecmp(key, "other_log_enabled")) {
        /* other_log_enabled yes
           other_log_disabled no */
        _cfg_read_bool(&buf, &(class ? class : def)->other_log_enabled);

      } else if (!strcasecmp(key, "other_log_dir")) {
        /* other_log_dir none
           other_log_dir ""    # same as none
           other_log_dir "/log"
           other_log_dir "~/logs" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
          
        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->other_log_dir);
        (class ? class : def)->other_log_dir = str;

      } else if (!strcasecmp(key, "other_log_program")) {
        /* other_log_program none
           other_log_program ""    # same as none
           other_log_program "/logprog"
           other_log_program "~/logprog" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
          
        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->other_log_program);
        (class ? class : def)->other_log_program = str;

      } else if (!strcasecmp(key, "other_log_always")) {
        /* other_log_always yes
           other_log_always no */
        _cfg_read_bool(&buf, &(class ? class : def)->other_log_always);

      } else if (!strcasecmp(key, "other_log_timestamp")) {
        /* other_log_timestamp yes
           other_log_timestamp no */
        _cfg_read_bool(&buf, &(class ? class : def)->other_log_timestamp);

      } else if (!strcasecmp(key, "other_log_maxsize")) {
        /* other_log_maxsize 128
           other_log_maxsize 0 */
        _cfg_read_numeric(&buf, &(class ? class : def)->other_log_maxsize);

      } else if (!strcasecmp(key, "other_log_recall")) {
        /* other_log_recall 128
           other_log_recall 0
           other_log_recall -1 */
        _cfg_read_numeric(&buf, &(class ? class : def)->other_log_recall);

      } else if (!strcasecmp(key, "motd_logo")) {
        /* motd_logo yes
           motd_logo no */
        _cfg_read_bool(&buf, &(class ? class : def)->motd_logo);

      } else if (!strcasecmp(key, "motd_file")) {
        /* motd_file none
           motd_file ""   # same as none
           motd_file "/file"
           motd_file "~/file" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->motd_file);
        (class ? class : def)->motd_file = str;

      } else if (!strcasecmp(key, "motd_stats")) {
        /* motd_stats yes
           motd_stats no */
        _cfg_read_bool(&buf, &(class ? class : def)->motd_stats);

      } else if (!strcasecmp(key, "allow_persist")) {
        /* allow_persist yes
           allow_persist no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_persist);

      } else if (!strcasecmp(key, "allow_jump")) {
        /* allow_jump yes
           allow_jump no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_jump);

      } else if (!strcasecmp(key, "allow_jump_new")) {
        /* allow_jump_new yes
           allow_jump_new no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_jump_new);

      } else if (!strcasecmp(key, "allow_host")) {
        /* allow_host yes
           allow_host no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_host);

      } else if (!strcasecmp(key, "allow_die")) {
        /* allow_die yes
           allow_die no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_die);

      } else if (!class && !strcasecmp(key, "connection")) {
        /* connection {
             :
             :
           } */

        if (*buf != '{') {
          /* Connection is a bit special, because the only valid value is '{'
             the internals are handled elsewhere */
          error("Expected open brace for key '%s' at line %ld of %s",
                key, line, filename);
          valid = 0;
          break;
        }
        buf++;

        /* Allocate memory, it'll be filled later */
        class = (struct ircconnclass *)malloc(sizeof(struct ircconnclass));
        memcpy(class, def, sizeof(struct ircconnclass));
        class->server_port = (def->server_port
                              ? x_strdup(def->server_port) : 0);
        class->drop_modes = (def->drop_modes
                             ? x_strdup(def->drop_modes) : 0);
        class->refuse_modes = (def->refuse_modes
                               ? x_strdup(def->refuse_modes) : 0);
        class->local_address = (def->local_address
                                ? x_strdup(def->local_address) : 0);
        class->away_message = (def->away_message
                               ? x_strdup(def->away_message) : 0);
        class->quit_message = (def->quit_message
                               ? x_strdup(def->quit_message) : 0);
        class->attach_message = (def->attach_message
                                 ? x_strdup(def->attach_message) : 0);
        class->detach_message = (def->detach_message
                                 ? x_strdup(def->detach_message) : 0);
        class->chan_log_dir = (def->chan_log_dir
                               ? x_strdup(def->chan_log_dir) : 0);
        class->chan_log_program = (def->chan_log_program
                                   ? x_strdup(def->chan_log_program) : 0);
        class->other_log_dir = (def->other_log_dir
                                ? x_strdup(def->other_log_dir) : 0);
        class->other_log_program = (def->other_log_program
                                    ? x_strdup(def->other_log_program) : 0);
        class->motd_file = (def->motd_file ? x_strdup(def->motd_file) : 0);

      } else if (class && !strcasecmp(key, "password")) {
        /* connection {
             :
             password "foo"
             :
           } */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free(class->password);
        class->password = str;

      } else if (class && !strcasecmp(key, "server")) {
        /* connection {
             :
             server "irc.linux.com"
             server "irc.linux.com:6670"     # Port other than default
             server "irc.linux.com:6670:foo" # Port and password
             server "irc.linux.com::foo"     # Password and default port
             :
           } */
        struct strlist *s;
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        s = (struct strlist *)malloc(sizeof(struct strlist));
        s->str = str;
        s->next = 0;

        if (class->servers) {
          struct strlist *ss;

          ss = class->servers;
          while (ss->next)
            ss = ss->next;

          ss->next = s;
        } else {
          class->servers = s;
        }

      } else if (class && !strcasecmp(key, "from")) {
        /* connection {
             :
             from "static-132.myisp.com"    # Static hostname
             from "*.myisp.com"             # Masked hostname
             from "192.168.1.1"             # Specific IP
             from "192.168.*"               # IP range
             :
           } */
        struct strlist *s;
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        s = (struct strlist *)malloc(sizeof(struct strlist));
        s->str = str;
        s->next = class->masklist;
        class->masklist = s;

      } else if (class && !strcasecmp(key, "join")) {
        /* connection {
             :
             join #foo
             join #foo,#bar
             :
           } */
        struct strlist *s;
        char *str, *orig;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        orig = str;
        while (str && strlen(str)) {
          char *ptr;

          ptr = strchr(str, ',');
          if (ptr)
            *(ptr++) = 0;

          s = (struct strlist *)malloc(sizeof(struct strlist));
          s->str = x_strdup(str);
          s->next = 0;

          if (class->channels) {
            struct strlist *ss;

            ss = class->channels;
            while (ss->next)
              ss = ss->next;

            ss->next = s;
          } else {
            class->channels = s;
          }

          str = ptr;
        }
        free(orig);

      } else if (class && !strcmp(key, "}")) {
        /* No auto-connect?  Then we *need* jump */
        if (!class->server_autoconnect)
          class->allow_jump = 1;

        /* Check that a password and at least one server were defined */
        if (!class->password) {
          error("Connection class defined without password "
                "before line %ld of %s", line, filename);
          valid = 0;
        } else if (!class->servers
                   && (class->server_autoconnect || !class->allow_jump_new)) {
          error("Connection class defined without a server "
                "before line %ld of %s", line, filename);
          valid = 0;
        }

        /* Add to the list of servers if valid, otherwise free it */
        if (valid) {
          class->orig_local_address = (class->local_address
                                       ? x_strdup(class->local_address) : 0);
          class->next_server = class->servers;
          class->next = connclasses;
          connclasses = class;
          class = 0;
        } else {
          ircnet_freeconnclass(class);
          class = 0;
          break;
        }

      } else {
        /* Bad key! */
        error("Unknown config file key '%s' at line %ld of %s",
              key, line, filename);
        valid = 0;
        break;
      }

      /* Skip whitespace.  The only things that can trail are comments
         and close braces, and we re-pass to do those */
      buf += strspn(buf, WS);
      if (*buf && (*buf != '#') && (*buf != '}')) {
        error("Unexpected data at and of line %ld of %s", line, filename);
        valid = 0;
        break;
      }
    }
  }

  /* Argh, untidy stuff left around */
  if (class) {
    ircnet_freeconnclass(class);
    class = 0;
    if (valid) {
      error("Unmatched open brace in %s", filename);
      valid = 0;
    }
  }

  fclose(fd);
  free(def->server_port);
  free(def->drop_modes);
  free(def->refuse_modes);
  free(def->local_address);
  free(def->away_message);
  free(def->quit_message);
  free(def->attach_message);
  free(def->detach_message);
  free(def->chan_log_dir);
  free(def->chan_log_program);
  free(def->other_log_dir);
  free(def->other_log_program);
  free(def->motd_file);
  return (valid ? 0 : -1);
}

/* Read a boolean value */
static int _cfg_read_bool(char **buf, int *val) {
  char *ptr;

  ptr = *buf;
  *buf += strcspn(*buf, WS);
  *((*buf)++) = 0;

  if (!strcasecmp(ptr, "yes")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "true")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "y")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "t")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "no")) {
    *val = 0;
  } else if (!strcasecmp(ptr, "false")) {
    *val = 0;
  } else if (!strcasecmp(ptr, "n")) {
    *val = 0;
  } else if (!strcasecmp(ptr, "f")) {
    *val = 0;
  } else {
    *val = (atoi(ptr) ? 1 : 0);
  }

  return 0;
}

/* Read a numeric value */
static int _cfg_read_numeric(char **buf, long *val) {
  char *ptr;

  ptr = *buf;
  *buf += strcspn(*buf, WS);
  *((*buf)++) = 0;

  *val = atol(ptr);

  return 0;
}

/* Read a string value */
static int _cfg_read_string(char **buf, char **val) {
  char *ptr;

  if (**buf == '"') {
    ptr = ++(*buf);

    while (1) {
      *buf += strcspn(*buf, "\"");
      if (**buf != '"') {
        return -1;
      } else if (*(*buf - 1) == '\\') {
        (*buf)++;
        continue;
      } else {
        break;
      }
    }
  } else {
    ptr = *buf;
    *buf += strcspn(*buf, WS);
  }
  *((*buf)++) = 0;

  *val = x_strdup(ptr);

  return 0;
}
