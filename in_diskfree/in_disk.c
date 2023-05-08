/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2022 The Fluent Bit Authors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/flb_pack.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "in_disk.h"

#include <mntent.h>
#include <sys/statvfs.h>

#define LINE_SIZE 256
#define BUF_SIZE  32

static int update_disk_stats(struct flb_in_diskfree_info *info, struct mntent *mount_entry)
{
    struct statvfs s;

    if (statvfs(mount_entry->mnt_dir, &s) != 0) {
        perror(mount_entry->mnt_dir);
        return -1;
    }
    // same logic as busybox df implementation
    /* Some uclibc versions were seen to lose f_frsize
    * (kernel does return it, but then uclibc does not copy it)
    */
    if (s.f_frsize == 0)
        s.f_frsize = s.f_bsize;
    info->f_frsize = s.f_frsize;
    info->f_blocks = s.f_blocks;
    info->f_bfree = s.f_bfree;
    info->f_bavail = s.f_bavail;

    return 0;
}

static FILE* open_mount_table() {
    FILE *mount_table = NULL;
    mount_table = setmntent("/proc/mounts", "r");
    if (mount_table == NULL) {
        flb_errno();
    }
    return mount_table;
}

/* cb_collect callback */
static int in_disk_collect(struct flb_input_instance *i_ins,
                           struct flb_config *config, void *in_context)
{
    unsigned long              write_total;
    unsigned long              read_total;
    int                        entry;
    struct flb_in_diskfree_config *ctx;
    int                        ret;
    int                        i;
    (void) *config;

    ret = 0;
    ctx = (struct flb_in_diskfree_config *) in_context;

    /* The type of sector size is unsigned long in kernel source */
    read_total = 0;
    write_total = 0;

    FILE *mount_table = open_mount_table();
    if (mount_table == NULL) return -1;

    struct mntent *mount_entry;
    struct flb_in_diskfree_info info;

    while (1) {
        mount_entry = getmntent(mount_table);
        if (!mount_entry) {
            endmntent(mount_table);
            break;
        }
        if (ctx->mount_point != NULL && strcmp(mount_entry->mnt_dir, ctx->mount_point) != 0)
            continue;
        if (update_disk_stats(&info, mount_entry) < 0)
            continue;
        if (info.f_blocks == 0 && !ctx->show_all)
            continue;
        if (ctx->fs_type != NULL && strcmp(mount_entry->mnt_type, ctx->fs_type) != 0)
            continue;

        flb_plg_trace(i_ins, "%s - %s %s %s %d %d %d %d %d", __FUNCTION__,
                        mount_entry->mnt_fsname, mount_entry->mnt_dir, mount_entry->mnt_type,
                        info.f_frsize, info.f_blocks, info.f_bfree, info.f_bavail);

        ret = flb_log_event_encoder_begin_record(&ctx->log_encoder);

        if (ret == FLB_EVENT_ENCODER_SUCCESS) {
            ret = flb_log_event_encoder_set_current_timestamp(
                    &ctx->log_encoder);
        }

        if (ret == FLB_EVENT_ENCODER_SUCCESS) {
            ret = flb_log_event_encoder_append_body_values(
                    &ctx->log_encoder,
                    FLB_LOG_EVENT_CSTRING_VALUE("mnt_dir"),
                    FLB_LOG_EVENT_CSTRING_VALUE(mount_entry->mnt_dir),

                    FLB_LOG_EVENT_CSTRING_VALUE("f_frsize"),
                    FLB_LOG_EVENT_UINT64_VALUE(info.f_frsize),

                    FLB_LOG_EVENT_CSTRING_VALUE( "f_blocks"),
                    FLB_LOG_EVENT_UINT64_VALUE(info.f_blocks),

                    FLB_LOG_EVENT_CSTRING_VALUE("f_bfree"),
                    FLB_LOG_EVENT_UINT64_VALUE(info.f_bfree),

                    FLB_LOG_EVENT_CSTRING_VALUE("f_bavail"),
                    FLB_LOG_EVENT_UINT64_VALUE(info.f_bavail));
        }


        if (ret == FLB_EVENT_ENCODER_SUCCESS) {
            ret = flb_log_event_encoder_commit_record(&ctx->log_encoder);
        }

        if (ret == FLB_EVENT_ENCODER_SUCCESS) {
            flb_input_log_append(i_ins, NULL, 0,
                                 ctx->log_encoder.output_buffer,
                                 ctx->log_encoder.output_length);

            ret = 0;
        }
        else {
            flb_plg_error(i_ins, "Error encoding record : %d", ret);

            ret = -1;
        }

        flb_log_event_encoder_reset(&ctx->log_encoder);
    }

    return 0;
}

static int configure(struct flb_in_diskfree_config *disk_config,
                     struct flb_input_instance *in)
{
    (void) *in;
    int entry = 0;
    int i;
    int ret;

    /* Load the config map */
    ret = flb_input_config_map_set(in, (void *)disk_config);
    if (ret == -1) {
        flb_plg_error(in, "unable to load configuration.");
        return -1;
    }

    FILE *mount_table = open_mount_table();
    if (mount_table == NULL) {
        flb_plg_error(in, "unable to list mounts.");
        return -1;
    }

    /* interval settings */
    if (disk_config->interval_sec <= 0 && disk_config->interval_nsec <= 0) {
        /* Illegal settings. Override them. */
        disk_config->interval_sec = atoi(DEFAULT_INTERVAL_SEC);
        disk_config->interval_nsec = atoi(DEFAULT_INTERVAL_NSEC);
    }

    return 0;
}

/* Initialize plugin */
static int in_disk_init(struct flb_input_instance *in,
                        struct flb_config *config, void *data)
{
    struct flb_in_diskfree_config *disk_config = NULL;
    int ret = -1;

    /* Allocate space for the configuration */
    disk_config = flb_calloc(1, sizeof(struct flb_in_diskfree_config));
    if (disk_config == NULL) {
        return -1;
    }

    /* Initialize head config */
    ret = configure(disk_config, in);
    if (ret < 0) {
        goto init_error;
    }

    flb_input_set_context(in, disk_config);

    ret = flb_input_set_collector_time(in,
                                       in_disk_collect,
                                       disk_config->interval_sec,
                                       disk_config->interval_nsec, config);
    if (ret < 0) {
        flb_plg_error(in, "could not set collector for disk input plugin");
        goto init_error;
    }

    ret = flb_log_event_encoder_init(&disk_config->log_encoder,
                                     FLB_LOG_EVENT_FORMAT_DEFAULT);

    if (ret != FLB_EVENT_ENCODER_SUCCESS) {
        flb_plg_error(in, "error initializing event encoder : %d", ret);

        goto init_error;
    }

    return 0;

  init_error:
    flb_free(disk_config);
    return -1;
}

static int in_disk_exit(void *data, struct flb_config *config)
{
    (void) *config;
    struct flb_in_diskfree_config *disk_config = data;

    flb_log_event_encoder_destroy(&disk_config->log_encoder);

    flb_free(disk_config);
    return 0;
}

/* Configuration properties map */
static struct flb_config_map config_map[] = {
    {
      FLB_CONFIG_MAP_INT, "interval_sec", DEFAULT_INTERVAL_SEC,
      0, FLB_TRUE, offsetof(struct flb_in_diskfree_config, interval_sec),
      "Set the collector interval"
    },
    {
      FLB_CONFIG_MAP_INT, "interval_nsec", DEFAULT_INTERVAL_NSEC,
      0, FLB_TRUE, offsetof(struct flb_in_diskfree_config, interval_nsec),
      "Set the collector interval (nanoseconds)"
    },
    {
      FLB_CONFIG_MAP_STR, "mount_point", (char *)NULL,
      0, FLB_TRUE, offsetof(struct flb_in_diskfree_config, mount_point),
      "Set the mount point"
    },
    {
      FLB_CONFIG_MAP_STR, "fs_type", (char *)NULL,
      0, FLB_TRUE, offsetof(struct flb_in_diskfree_config, fs_type),
      "Collect only for this filesystem (ex: ext4)"
    },
    {
      FLB_CONFIG_MAP_BOOL, "show_all", "false",
      0, FLB_TRUE, offsetof(struct flb_in_diskfree_config, show_all),
      "Collect all mount points, even when kernel reports 0 total blocks"
    },
    /* EOF */
    {0}
};

struct flb_input_plugin in_diskfree_plugin = {
    .name         = "diskfree",
    .description  = "Diskstats (storage)",
    .cb_init      = in_disk_init,
    .cb_pre_run   = NULL,
    .cb_collect   = in_disk_collect,
    .cb_flush_buf = NULL,
    .cb_exit      = in_disk_exit,
    .config_map   = config_map
};
