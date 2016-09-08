[![Build Status](https://travis-ci.org/AlxHnr/nano-backup.svg?branch=master)](https://travis-ci.org/AlxHnr/nano-backup)
[![codecov.io](https://codecov.io/github/AlxHnr/nano-backup/coverage.svg?branch=master)](https://codecov.io/github/AlxHnr/nano-backup?branch=master)

Nano-backup provides a precise way to track files. It was intended for
power-users who want to keep track of their fully customized posix system.
It makes it easy to backup only the least amount of files required to
restore your system. Nano-backup does not try to replace existing backup
tools and focuses only on local backups. It stores full snapshots of files
and may not be suited for backing up large VM images.

Building nano-backup requires a C99 compiler,
[pkg-config](http://www.freedesktop.org/wiki/Software/pkg-config/) and
[OpenSSL](https://www.openssl.org/). It can be installed by cloning this
repository and running `make` inside it. If the build succeeds, simply copy
`build/nb` to a directory like `/usr/bin`.

**Note:** Gentoo users can install nano-backup directly from my
[overlay](https://github.com/AlxHnr/gentoo-overlay).

## Usage

To create a backup repository just create a new directory:

```sh
mkdir repo/
```

The repository can be configured via the file `config` inside it. Here is
an example for backing up two directories:

```
[copy]
/home/user/Videos
/home/user/Pictures
```

The first line sets the copy [policy](#policies). The other lines are
absolute paths to files or directories which should be backed up. To do a
backup, pass the repository to nano-backup:

```sh
nb repo/
```

To prevent files from being backed up, set the ignore policy. This allows
specifying regular expressions, which will be matched against full,
absolute filepaths. They must be valid POSIX extended regular expressions:

```
[ignore]
\.(pyc|class)$
^/home/user/.+~$
```

Regular expressions can also be used for matching files you want to backup.
Just prefix a pattern with an additional slash:

```
[copy]
/home/user/Pictures//\.(png|jpg)$
```

**Note**: A regular expression can not contain a slash.

### Restoring files

To restore a file or directory, pass its full or relative path to
nano-backup:

```sh
nb repo/ 0 file.txt
```

The _0_ is the id of the latest backup. The backup before it would be _1_,
etc.

**Note**: The number is only relevant for tracked paths since only they
have a history.

## Policies

Policies specify how files should be backed up. They apply only to the last
element of a path:

```
[policy]
/home/user/last-element
```

All its parent directories will be backed up silently. Paths can inherit
policies from their parents:

```
[mirror]
/home

[track]
/home/user/.config/
```

In the example above the files in `user` will be mirrored, while the files
in `.config` will be tracked.

Policy name | Description
------------|-------------
copy        | Backup only the latest version of a file.
mirror      | Like copy, but if a file gets removed from the filesystem, it will also be removed from the backup.
track       | Keep a history of every change, including modification timestamps, owner, group and permission bits.
ignore      | Not really a policy, but allows to specify regular expressions for excluding files. It has a lower priority than the other policies and only matches files which have not been matched already.

## Frequently asked questions

### How do i synchronize two repositories?

Here is an example for the repositories _old/_ and _current/_:

```sh
cp -rn current/* old/
cp current/{config,metadata} old/
nb old/ gc
```
