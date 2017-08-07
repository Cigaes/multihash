multihash
=========

`multihash` is a tool to compute various hashes on collections of files.

It is optimized for I/O-bound large files collections by computing the
hashes in parallel and caching the results.

Supported hashes
----------------

* CRC-32
* MD5
* SHA-1
* SHA-256
* SHA-512

Features
--------

* Parallel hashing.
* Cached results.
* Recursive exploration with JSON output of hashes and metadata.
* Hashing of the files in a tar archive.

Building
--------

`multihash` requires GNU make, OpenSSL and the Berkeley DB library and a
system compatible with Single Unix v4. It has been tested with GNU/Linux.

It does not use a `configure` script but supports the usual make variables:
`CFLAGS`, `LDFLAGS`, `LIBS`, `PREFIX`, `DESTDIR`. Build options can be
changed directly in the `Makefile` or passed to the make command-line:

```
make LDFLAGS=-L/opt/openssl/lib PREFIX=/opt/multihash DESTDIR=/tmp/install
```

Out-of-tree build is supported using the `configure` target. Options passed
to that target are stored permanently in the local Makefile (except
`DESTDIR`):

```
make -f ~/src/multihash/Makefile PREFIX=/opt/multihash configure
```

Copyright
---------

Copyright (c) 2017 Nicolas George <george@nsup.org>

This program is free software; you can redistribute it and/or modify it
under the terms of the [GNU General Public License version 2][GPLv2]
as published by the Free Software Foundation.

  [GPLv2]: http://www.gnu.org/licenses/old-licenses/gpl-2.0.html

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.
