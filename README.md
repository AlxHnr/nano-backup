Nano-backup grants the user precise control about changes in files and
directories and to which degree a history of them should be kept.

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

The repository can be configured by creating a file named `conf` inside it.

To tell nano-backup to simply copy files into the repository, the backup
policy _copy_ must be set in the config file. After that, full filepaths
can be specified for files that should be backed up:

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

To prevent files from being backed up, set the _ignore_ policy in the
config file. Every line after that is a POSIX extended regex and will be
matched against full, absolute filepaths:

```
[ignore]
\.(pyc|class)$
^/home/user/.+~$
```

To do a backup, simply pass the repository path to nano-backup:

```sh
nb repo/
```
