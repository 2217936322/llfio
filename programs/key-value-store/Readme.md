Herein lies an exploratory toy ACID key-value store written using
AFIO which lets you look up any BLOB value from some 128-bit key, and to
update as an atomic transaction up to 65,535 key-values at once.

It is purely to test the feasibility of one approach to implementing such
a store, and to test AFIO's design. Nobody should use this store for
anything serious.

## Todo:
- [x] Add sparse file creation on Windows to AFIO and see how the
benchmarks fare.
- [x] Add key-value deletion.
- [x] Atomic append should issue gather buffers of `IOV_MAX`
- [x] Optionally use mmaps to extend smallfile instead of atomic appends.
Likely highly racy on Linux due to kernel bugs :)
- [ ] Use mmaps for all smallfiles
  - Windows x64 provides 128Tb of address space
  - Linux x64 provides 128Tb of address space
  - On adoption of smallfile, would need to parse backwards from end to
  find last used record.
- [ ] Online free space consolidation (copy early still in use records
into new small file, update index to use new small file)
  - [ ] Per 1Mb free space consolidated, punch hole
- [ ] Need some way of detecting and breaking sudden process exit during
index update.

## Benchmarks:
- 1Kb values Windows with NTFS, no integrity, no durability:
  ```
  Inserting 1M key-value pairs ...
  Inserted at 157183 items per sec
  Retrieving 1M key-value pairs ...
  Fetched at 612745 items per sec
  ```
- 1Kb values Windows with NTFS, integrity, no durability:
  ```
  Inserting 1M key-value pairs ...
  Inserted at 145053 items per sec
  Retrieving 1M key-value pairs ...
  Fetched at 542005 items per sec
  ```
- 1Kb values Linux with ext4, no integrity, no durability:
  ```
  Inserting 1M key-value pairs ...
  Inserted at 656598 items per sec
  Retrieving 1M key-value pairs ...
  Fetched at 1945525 items per sec
  ```
- 1Kb values Linux with ext4, integrity, no durability:
  ```
  Inserting 1M key-value pairs ...
  Inserted at 581057 items per sec
  Retrieving 1M key-value pairs ...
  Fetched at 1519756 items per sec
  ```
  
- 16 byte values Windows with NTFS, no integrity, no durability:
  ```
  Inserting 1M key-value pairs ...
  Inserted at 259201 items per sec
  Retrieving 1M key-value pairs ...
  Fetched at 700770 items per sec
  ```
- 16 byte values Linux with ext4, no integrity, no durability:
  ```
  Inserting 1M key-value pairs ...
  Inserted at 1118568 items per sec
  Retrieving 1M key-value pairs ...
  Fetched at 2898550 items per sec
  ```