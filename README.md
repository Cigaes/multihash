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

Copyright
---------

Copyright (c) 2016 Nicolas George <george@nsup.org>

This program is free software; you can redistribute it and/or modify it
under the terms of the [GNU General Public License version 2]
(http://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
as published
by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.
