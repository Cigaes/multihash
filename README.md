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
