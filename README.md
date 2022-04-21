# fluent-bit-in-diskfree

This is fluent-bit plugin to collect disk blocks used.

This is based on the core plugin [in_disk](https://github.com/fluent/fluent-bit/blob/master/plugins/in_disk/in_disk.c) that collect I/O stats. This one fills the gap of a missing space used collector.

It reports the same information as `stat` does:

```
$ docker run --rm \
			-v $(pwd)/dist:/myplugin \
			fluent/fluent-bit:1.9.2 \
			/fluent-bit/bin/fluent-bit \
			-f 1 \
			-e /myplugin/flb-in_diskfree.so \
			-i diskfree -q\
			-o stdout -m '*' \
			-o exit -m '*'
...
[0] diskfree.0: [1650554975.926618100, {"mnt_dir"=>"/", "f_frsize"=>4096, "f_blocks"=>15313873, "f_bfree"=>1036829, "f_bavail"=>251498}]
...
```

```
$ stat -f /
File: "/"
  ID: f2d4f41b8164f6ea Namelen: 255     Type: UNKNOWN
Block size: 4096
Blocks: Total: 15313873   Free: 1036829    Available: 251498
Inodes: Total: 3907584    Free: 1770209
```

`df` output for this is:

```
$ df -B4096 /
Filesystem           4K-blocks      Used Available Use% Mounted on
overlay               15313873  14277045    251497  98% /
```

Looking at [busybox df implementation](https://github.com/mirror/busybox/blob/94c78aa0b91f2150bd038866addf3d0ee69474a8/coreutils/df.c#L270-L271)(as it is cleaner than others), you can get the same values as such:

| df        | in_diskfree                                              | value (example) |
| --------- | -------------------------------------------------------- | --------------- |
| Blocks    | `f_blocks`                                               | 15313873        |
| Available | `f_bavail`                                               | 251497          |
| Used      | `f_blocks - f_bfree`                                     | 14277044        |
| Use%      | `(f_blocks - f_bfree) / (f_blocks - f_bfree + f_bavail)` | 98.26%          |


## Configuration

| Key  | Description | default |
| ---  | ----------- | ------- |
| Interval_Sec | Polling interval (seconds). | 1 |
| Interval_NSec| Polling interval (nanosecond). | 0 |
| Mount_Point | Mount point to filter (eg: `/`). | all mounts |
| Fs_Type | Collect only moount points with this filesystem (eg: `ext4`) | all mounts |
| Show_All | Collect also mount points that report 0 blocks as total | off |

## Usage

Simplest (all mounts with more than 0 blocks):

```ini
[INPUT]
    Name diskfree
```

Only ext4 filesystems
```ini
[INPUT]
    Name diskfree
    Fs_Type ext4
```

Only root fs

```ini
[INPUT]
    Name diskfree
    Mount_Point /
```
