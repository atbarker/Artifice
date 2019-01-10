# Artifice

The Artifice File System is a pseudo file system which exists in the free space of another file system. By doing so, Artifice remains hidden from suspecting users and hence can be used to store information with concrete deniability. Artifice is implemented using device mapper, and it performs a block to block mapping of information.

For contributions to this project, please contact `Yash Gupta` and `Austen Barker` {ygupta,atbarker}@ucsc.edu.

## Build

```
$ make
$ sudo insmod dm-afs.ko afs_debug_mode=1
$ sudo rmmod dm_afs

Please look at the Makefile targets `debug_new`, `debug_access` and `debug_end` for information on how to setup a dm-target.
```

## Guidelines

All code pushed to the repository is formatted using `clang-format`. Style guide parameters are as follows:

```
{
    BasedOnStyle: WebKit,
    AlignTrailingComments: true,
    AllowShortIfStatementsOnASingleLine: true,
    AlwaysBreakAfterDefinitionReturnType: true,
    PointerAlignment: Right
}
```

## Design

The design portion of this markdown is not intended to be reference information. It won't make sense unless you read from start to end.

This design document has **NOT** been updated for information on a `shadow` (nested) Artifice instance.

Something to make clear before we proceed. A block, or a pointer to a block, is represented by a `32 bit` integer in Artifice. This means that we can, at max, reference `~4 billion` blocks. Since a block is `4KB`, it means we can reference, at max, a disk of size `16TB`.

### Structure

Before we can explain the design of Artifice, it is important to understand it's structure and its usage. Artifice acts like a virtual block device on top of an existing device. 

Hence, when you create a dm-target for `artifice`, you will have a block device which may look like `/dev/mapper/artifice` (depending on how you name your instance). Any kind of I/O on this virtual block device is sent over to the Artifice kernel module. This module intercepts this IO, remaps it to the location it needs to be remapped to, and then "submits" it. 

Accordingly, the idea is that one can technically format `/dev/mapper/artifice` with a file system such as "FAT32", and Artifice can remap each of the FAT32 writes over to an underlying physical block device (such as `/dev/sdb`). Based on our design, we will only utilize the free blocks in `/dev/sdb`, hence, `/dev/sdb` needs to be formatted with a supported file system, and there should be enough free space left in that underlying file system.

Support for underlying file systems is added through `modules`.

### Modules

By now you should be comfortable with understanding the hierarchy of file systems in Artifice. There is the file system which is used on `/dev/mapper/artifice` virtual block device (we call this the active file system), and then there is the file system present on the underlying physical block device such as `/dev/sdb` (we call this the passive file system). The idea is that writes onto the active file system are mapped into the free space of the passive file system.

Since the writes are simple mappings, theoretically, you could use _any_ file system as the active file system. This is not entirely true in practice however, since certain file systems (such as EXT4) require some extra support like ZONE_INFORMATION. Currently, Artifice is unable to map these special requests onto a physical device.

On the other hand, we need discrete support for the passive file system. This is where `Artifice Modules` come into play. A modules task, given a physical block device, is to figure out whether or not a particular fule system exists on the physical block device. Moreover, if a module detects that a certain file system is present, then it needs to provide pointers to the free blocks in the file system to be used by Artifice. If a module cannot detect the presence of a particular file system on a physical block device, then it simply returns a `false` and does not need to fill up free block information.

Currently, we plan to add modules to support `FAT32`, `EXT4` and `NTFS`.

### Entropy Blocks

Encryption only goes so far in keeping the data safe. Unfortunately, encrypted data is still visible and makes a user suseptible to a rubber-hose attack. To prevent the data from even being detected, we use `entropy blocks`.

When a user creates a new instance of Artifice, they are required to specify an `entropy directory` which contains a number of high-entropy files (such as DRM protected media). The idea is that each block which is mapped through Artifice is XOR'ed with some `entropy block` (which will belong to one of the files in the `entropy directory`) and without these entropy blocks, there is no way left to reconstruct the original data.

When Artifice is first started, it reads the `entropy directory` and stores the name for each file into a hash table. For each data block that is mapped, a single `entropy file` is used. To reduce space consumption (since we need to map a lot of data blocks), an `8` byte hash of the filename is stored instead of the filename itself. This hash also acts as the key into the entropy hash table.

### Carrier Blocks

To provide obfuscation and survivability, Artifice maps data blocks in the form of carrier blocks. Each data block is secret split into `m` carrier blocks, of which `n` are required for reconstruction. This provides us with `m-n` redundant blocks. Moreover, if one has less than `n` blocks, then the data block _cannot_ be reconstructed.

Each carrier block is XOR'ed with an `entropy block` to provide more obfuscation. As explained above, without the `entropy block`, recovering the carrier block is impossible. The number of carrier blocks is user specified (the default is `4`).

Given this, we can finally look at the metadata which is stored for a `single` data block:
```
<Carrier block pointer, Entropy block pointer, 2 Byte checksum of carrier block>
<Carrier block pointer, Entropy block pointer, 2 Byte checksum of carrier block>
...
<Carrier block pointer, Entropy block pointer, 2 Byte checksum of carrier block>
<Hash of the data block>
<Hash of the entropy file name>
```

The `entropy block pointer` is nothing more than a byte/block offset into the `entropy file` for that particular carrier block. We do not need to store sizes since each block is `4KB`.

This metadata for a single data block is known as a `map entry`, since it signifies a single entry into the `Artifice Map`.

### Artifice Map

The Artifice map is the _huge_ chunk of memory which contains a `map entry` for every possible data block. For example, if you create an Artifice instance of size 4MB, then the Artifice map will contain `1024` map entries (we map per 4KB block).

For any read request onto `/dev/mapper/artifice`, we index into the Artifice map with the given block number, combine all the carrier blocks into a data block and then return that to the user.

For any write request onto `/dev/mapper/artifice`, we index into the Artifice mao with the given block number, split the data block into the required number of carrier blocks and then return to the user.

This all sounds well and good, but there is one huge problem: We need to store the Artifice Map for each instance since it contains all our mapping metadata. However, the Artifice map consumes a lot of memory (and it clearly has to). The problem is that since we are storing everything in the free blocks of the passive file system, we cannot always assume to find contiguous chunks of free blocks. Moreover, we would rather not store the map in contiguous chunks anyway since that would make them more likely to be overwritten at once by the passive file system. This is where `map blocks` and `pointer blocks` come in.

### Artifice Map Blocks and Pointer Blocks

Artifice map blocks are a way to split up the Artifice map into separate blocks instead of a contiguous chunk of memory. The idea is that each map block is nothing more than a 4KB block, and each block will store a certain amount of the map in it. For example, in the default configuration, a `map entry` is 64 bytes (16 bytes for the data hash, 8 bytes for the entropy file hash, and then 4 tuples of <carrier, entropy, checksum>, each being 10 bytes). Given this 64 byte `map entry`, a 4KB block can technically store 64 `map entries`. But, since we can always be overwritten by the passive file system, we need a way to know if we have been overwritten. Hence, the first 64 bytes in a `map block` are reserved for the hash of the rest of the `map block`. With that, it would mean that for a default configuration, a single `map block` stores `63 map entries`. 

Coming back to a previous example of a `4MB` Artifice instance, we required `1024` map entries. Since we can store `63` `map entries` in a single `map block`, we would overall require `~17 map blocks`. Please note that since the number of carrier blocks is user dependent, the size of a `map entry` is user dependent. Based on those parameters, the number of entries one could store in a `map block` could change as well.

So now that we have a way to break up the Artifice Map into separate blocks, we have to tackle the problem of referencing the `map blocks` themselves. We need a way to locate these `map blocks` and rebuild the `Artifice Map` every time an Artifice instance is mounted.

Since a `map block` is just a `4KB` block, a pointer to it consumes `4 bytes`. Hence, considering again a `4KB` block, we can store `1024` pointers to `map blocks` in a single block. This kind of a block, which stores pointers to `map blocks` is referred to as a `pointer block`. In reality however, a `pointer block` only stores pointers to `1019 map blocks`. The reason for that is because it also stores a `16` byte hash of the entire `pointer block` and a `4` byte pointer to the next `pointer block`. The reason we store a pointer to the next `pointer block` is simply because we may require more than one `pointer block` in an Artifice instance. Having a pointer also allows us to store `pointer blocks` in a non-contiguous manner.

Having understood all of this, there is a small optimization we are able to make. We store the pointers to the first `975 map blocks` in the `Artifice Super Block`. The super block also stores a pointer to the first `pointer block.`

### Artifice Super Block

The `Artifice Super Block` is another `4KB` block. It stores all the configuration information for the Artifice instance, including, but not limited to: Instance size, Entropy Directory, Reed-Solomon parameters, etc. Whenever an instance is mounted, the super block is used to confirm the size of the Artifice instance. If the size does not match the one stored in the super block, an error is issued and creation of the dm-target fails.

Since the super block has quite a bit of free space left (it is 4KB afterall), we decided that we can store some of the pointers to `map blocks` in the super block itself. With the space that we had left, we could store up to `975` pointers to `map blocks` and another `4` byte pointer to the first `pointer block` (which contains the rest of the `map block` pointers).

So in essense, it is the super block which allows us to locate and rebuild the metadata for Artifice. But the problem is the location of the super block itself. Clearly, it cannot be hardcoded as that would make it trivial to find it. Hence, what we do is `chain hashing`.

Every Artifice instance requires a passphrase. We hash this passphrase and use that hash as the location of the super block. If the location we acquired was not free, we hash the hash again, and repeat the process until we find a free block. This way, the only way to find the super block is to know the passphrase. Without the passphrase, it would be impossible to even detect the presence of the super block, and without the super block, it would be impossible to detect the presence of Artifice.

### Encryption

What kind of a security sub-system would not contain encryption? Artifice uses encrpytion for a very special purpose.

So far we have been saying that detecting the super block would be impossible without knowing the passphrase. But that is not entirely true, since one could perform a linear scan of a disk to find the super block. In fact, one could even detect the `pointer blocks` and the `map blocks` by performaing a linear scan since they are structured in predictable patterns. Far as the entropy blocks are contained, they are used to obfuscate only the carrier blocks, and not any of the Artifice metadata. That is because encryption is used to obfuscate all the metadata, including the `super block`, `pointer blocks`, and `map blocks`. The key is none other than the passphrase which is supplied when an Artifice instance is _created_ or _mounted_.

With encryption in place, the super block legitimately cannot be found unless a passphrase is known. Since encrypted data looks very much like random data, there are no patterns an adversary can be on the lookout for, even if they perform a full linear scan of a disk.

Put everything together, and we have `Artifice`.