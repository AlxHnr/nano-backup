[![Build Status](https://travis-ci.org/AlxHnr/nano-backup.svg?branch=master)](https://travis-ci.org/AlxHnr/nano-backup)

Nano-backup grants the user precise control about changes in files and
directories and to which extend a history of them should be kept.

**Warning**: Nano-backup is still under construction and most features
described here may not work yet.

It can be either installed from my
[Gentoo overlay](https://github.com/AlxHnr/gentoo-overlay) or by cloning
this repository and running `make` inside it. If the build succeeds,
simply copy `build/nb` to a directory like `/usr/bin`.

Nano-backup requires a POSIX.1-2001 conform OS and depends on
[OpenSSL](https://www.openssl.org/). Building the program requires a C99
Compiler and
[pkg-config](http://www.freedesktop.org/wiki/Software/pkg-config/).

## Usage

Creating a backup repository is not different from creating a new
directory:

```sh
mkdir repo/
```

The repository can be configured by creating a file named `config` inside
it.

To tell nano-backup to simply copy files into the repository, the backup
policy [copy](#copy) must be set in the config file. After that, full
filepaths can be specified for files that should be backed up:

```
[copy]
/home/user/Videos
/home/user/Pictures
```

To use POSIX extended regular expressions for file matching, prefix a
pattern with an additional slash. These patterns will only match file and
directory names:

```
/home/user/Pictures//\.(png|jpg)$
```

To prevent files from being backed up, set the [ignore](#ignore) policy in
the config file. Every line after that is a POSIX extended regex and will
be matched against full, absolute filepaths:

```
[ignore]
\.(pyc|class)$
^/home/user/.+~$
```

To do a backup, simply pass the repository path to nano-backup:

```sh
nb repo/
```

## Policies

Nano-backup can backup various files in different ways. Policies apply only
to the last element of a path:

```
/home/user/Pictures/last-element
```

All the parent directories of the last element will be backed up silently
without the users knowledge and will not have a history. Paths can inherit
policies from their parents:

```
[mirror]
/home

[track]
/home/user/.config/
```

In the example above, the files in `user` will be mirrored, while the files
in `.config` will be tracked.

### copy

This is the simplest policy. Every file/directory will be backed up
recursively without having a change history. If a file gets changed in the
filesystem, its backup gets overwritten.

### mirror

This policy is almost identical to [copy](#copy), but with the difference
that if a file gets removed from the filesystem, it will also be removed
from the backup.

### track

This policy will create a history of every change in a file or directory.
This includes metadata, like modification timestamps, owner, group and
permission bits.

### ignore

This is not really a policy, but allows to specify POSIX extended regular
expressions for excluding full paths from being backed up.
