```diff
- WARNING
```
This is an active research project and is still under development. As such there may be any number of incomplete, quickly written, or outright broken parts of this project. As with most research code the programs in this repository have not been thoroughly tested and if you intend to use this software do so at your own risk.

*DO NOT USE THIS TO HIDE SENSITIVE INFORMATION WITHOUT EXTENSIVE RED TEAM TESTING SPECIFIC TO YOUR USE CASE*

# Artifice

Artifice is a virtual block device/pseudo file system that exists in the free space of another file system. By doing so, Artifice remains hidden from suspecting users and hence can be used to store information with concrete deniability. Artifice is implemented using device mapper, and it performs a block to block mapping of information.

For contributions to this project, please contact `Austen Barker` at atbarker@ucsc.edu.

Artifice was developed at the UC Santa Cruz [Storage Systems Research Center](https://www.ssrc.ucsc.edu/index.html) (SSRC) with support from National  Science  Foundation  grant  number  IIP-1266400, and award CNS-1814347.

It is described in the workshop paper ["Artifice: A Deniable Steganographic File System"](https://www.ssrc.ucsc.edu/pub/barker-foci19.html) presented at [FOCI'19](https://www.usenix.org/conference/foci19) and the conference paper ["Artifice: Data in Disguise"](https://www.ssrc.ucsc.edu/pub/barker-msst20.html) presented at [MSST'20](https://storageconference.us/).

Some of the cryptographic implementations for Artifice are contained in [this repository](https://github.com/kyle-fredrickson/artifice-crypto).


### Collaborators

`Yash Gupta`, ygupta@ucsc.edu

`Eugene Chou` euchou@ucsc.edu

`Darrell Long` darrell@ucsc.edu

`Sabrina Au` scau@ucsc.edu

`Kyle Fredrickson` kyfredi@ucsc.edu

`James Houghton` jhoughton@virginia.edu

`Ethan Miller` elm@ucsc.edu

## Build

Artifice is tested on and designed for Linux kernel versions 4.4 to 5.0.

```
$ make
$ sudo insmod dm-afs.ko afs_debug_mode=1
$ sudo rmmod dm_afs
```
Please look at the Makefile targets `debug_create`, `debug_mount` and `debug_end` for information on how to setup a dm-target.

There are also a variety of other Makefile targets for benchmarking and IO testing that also appear with the `debug_*` prefix. 

## Design

![Artifice Design Overview](https://github.com/atbarker/Artifice/blob/master/doc/newartificediagram.png)

The design portion of this markdown is not intended to be reference information. It won't make sense unless you read from start to end.

This design document has **NOT** been updated for information on a `shadow` (nested) Artifice instance.

Something to make clear before we proceed. A block, or a pointer to a block, is represented by a `32 bit` integer in Artifice. This means that we can, at max, reference `~4 billion` blocks. Since a block is `4KB`, it means we can reference, at max, a disk of size `16TB`. `64 bit` block pointer support is pending.

### Structure

Before we can explain the design of Artifice, it is important to understand it's structure and its usage. Artifice acts like a virtual block device on top of an existing device. 

Hence, when you create a dm-target for `artifice`, you will have a block device which may look like `/dev/mapper/artifice` (depending on how you name your instance). Any kind of I/O on this virtual block device is sent over to the Artifice kernel module. This module intercepts this IO, remaps it to the location it needs to be remapped to, and then "submits" it. 

Accordingly, the idea is that one will format `/dev/mapper/artifice` with a file system such as "FAT32", and Artifice can remap each of the FAT32 writes over to an underlying physical block device (such as `/dev/sdb`). Based on our design, we will only utilize the free blocks in the file system on `/dev/sdb`, hence, `/dev/sdb` needs to be formatted with a supported file system, and there should be enough free space left in that underlying file system to fit the desired Artifice volume(s).

Support for underlying file systems is added through `modules`.

### Modules

By now you should be comfortable with understanding the hierarchy of file systems in Artifice. There is the file system which is used on `/dev/mapper/artifice` virtual block device (we call this the active file system), and then there is the file system present on the underlying physical block device such as `/dev/sdb` (we call this the passive file system). The idea is that writes onto the active file system are mapped into the free space of the passive file system.

Since the writes are simple mappings, theoretically, you could use _any_ file system as the active file system. This is not entirely true in practice however, since certain file systems (such as EXT4) require some extra support like ZONE_INFORMATION. Currently, Artifice is unable to map these special requests onto a physical device.

On the other hand, we need discrete support for the passive file system. This is where `Artifice Modules` come into play. A modules task, given a physical block device, is to figure out whether or not a particular fule system exists on the physical block device. Moreover, if a module detects that a certain file system is present, then it needs to provide pointers to the free blocks in the file system to be used by Artifice. If a module cannot detect the presence of a particular file system on a physical block device, then it simply returns a `false` and does not need to fill up free block information.

Currently, we have modules for `FAT32`, `NTFS`, and `EXT4`. Although we have planned support for `APFS`, APFS is a propietory file system by Apple, and as such its specifications are unclear. Support for log structured or flash specific file systems like `F2FS` or `YAFFS` is a long term goal.

### How do we keep mapped data safe, and unrecognizable: Information Dispersal Algorithms.

NOTE: Currently the Reed-Solomon+Entropy scheme is not included. It will be added back into the device mapper soon.

Since Artifice is mapping I/O from the active to the passive file system, the data which is being mapped would be clearly visible if one were to begin inspecting the physical device (`/dev/sdb`) directly. This presents a problem, since the existence of this data proves the existence of the Artifice subsystem. 

Managing different keys or IVs for each data block is resource intensive, and using a single key for the encryption of all data blocks is a security hazard. Hence, to provide obfuscation and security of the data, we use an information dispersal algorithm (IDA) to provide combinatoric security.

![Artifice Encoding Schemes](https://github.com/atbarker/Artifice/blob/master/doc/EncodingSchemes.png)

An easily known information dispersal algorithm is called Shamir Secret Sharing, which provides us with relatively strong combinatoric security. We can also utilize an algorithm that combines Rivest's All or Nothing Transform with Reed Solomon erasure codes to provide a similar threshold scheme (slightly weaker than Shamir's scheme with only computational security) but with vastly improves space efficiency.

There is another option for an information dispersal algorithm that involves combining existing sources of entropy on the disk (any random appearing file) with Reed-Solomon erasure codes. In this scheme when a user creates a new instance of Artifice, they are required to specify an `entropy directory` which contains a number of high-entropy files (such as DRM protected media). When a certain data block is to be mapped, a random file, termed the `entropy file`, is selected from within this directory and the data combined with random blocks from the `entropy file` through Reed-Solomon error correcting codes. For subsequent reads on the same data block, the filename and the block offset within the file is looked up in the metadata, and the same `entropy block` is used again to recover the data. Without access to this entropy information, it is impossible to recreate the data, depending on the configuration of the Reed-Solomon code word.

This is the main selling point for the use of `entropy blocks`. The fact is, a user can store the entropy files on a portable flash drive, and in case of an emergency, discard this flash drive. Now, even if their passphrase was to be compromised and an adversary was able to figure out the names of the entropy files, the adversary will still not have access to those files. And without access to those files, the adversary will not have access to the data.

However, filenames can be rather large and we need to preserve as much space as possible. Hence, we provide an optimization: When Artifice is first started, it reads the `entropy directory` and stores the name for each file into a hash table. To reduce space consumption, an `8` byte hash of the filename is stored instead of the filename itself. This hash also acts as the key into the entropy hash table.

### Carrier Blocks

To provide obfuscation and survivability, Artifice maps data blocks in the form of carrier blocks. Each data block is secret split into `m` carrier blocks, of which `n` are required for reconstruction. This provides us with `m-n` redundant blocks. Moreover, if one has less than `n` blocks, then the data block _cannot_ be reconstructed.

The number of carrier blocks and rebuild threshhold are user specified (the default is `4`).

Given this, we can finally look at the metadata which is stored for a `single` data block:
```
<Carrier block pointer, 2 Byte checksum of carrier block>
<Carrier block pointer, 2 Byte checksum of carrier block>
...
<Carrier block pointer, 2 Byte checksum of carrier block>
<Hash of the data block>
```

This metadata for a single data block is known as a `map entry`, since it signifies a single entry into the `Artifice Map`.

### Artifice Map

![Artifice Map](https://github.com/atbarker/Artifice/blob/master/doc/artifice_map.png)

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

### Encryption and Secret Sharing

What kind of a security sub-system would not contain encryption? Artifice uses encrpytion for a very special purpose.

So far we have been saying that detecting the super block would be impossible without knowing the passphrase. But that is not entirely true, since one could perform a linear scan of a disk to find the super block. In fact, one could even detect the `pointer blocks` and the `map blocks` by performaing a linear scan if they are structured in predictable patterns. In Artifice we encrypt the `superblock` and protect the rest of the metadata in much the same way as the data blocks. The key is none other than the passphrase which is supplied when an Artifice instance is _created_ or _mounted_.
When I was young, and I played all the sports, it didn't help one bit. Now that I'm old and sedentary, it has about the same severity when I'm off meds, but life is so much easier and better managed when I'm on meds.


With encryption in place, the super block cannot be found without the passphrase. Since encrypted data looks random, there are no patterns an adversary can be on the lookout for, even if they perform a full linear scan of a disk.

### SSD's and TRIM

An operating system lets an SSD know that data has been deleted and therefore a block is free for remapping and wear leveling through a special disk command called TRIM. TRIM poses a special problem for steganographic file systems because if TRIM is enabled on a system an adversary could cross reference public file system metadata with what is marked as used on the SSD. Differences could possibly give away the existence of a steganographic file system. Although most operating systems only emit TRIM commands periodically so there will likely always be a small number of blocks marked as free by a file system but not by the SSD. 

It would be a far easier solution to avoid the question of "how much of a difference is deniable?" and simply TRIM Artifice carrier blocks once written to the disk. The feasibility of this approach is dependent on the kind of TRIM implemented by an SSD. There are currently three types of TRIM supported by modern SSDs. Each pose relatively similar challenges for Artifice.

* Non-deterministic TRIM: Each read of a TRIMmed block may return different data. In this case Artifice simply has another possible source of overwrites. In this case the problem may be solved through storing additional carrier blocks to provide greater resiliency.
* Deterministic TRIM (DRAT): All read commands to a TRIMmed block return the same data. Much like before this may simply be another source of overwrites and the user should use a higher number of carrier blocks per code word to compensate.
* Deterministic Read Zero after TRIM (RZAT): All read commands to a TRIMmed block return only zeros. This is significantly more efficient than previous methods as the FTL need not even look at the raw flash and can return much faster for freed blocks. Although this may cause it to be impossible to retrieve Artifice data as any TRIMmed blocks will be all zeroes instead of the data stored in the flash cells. This could be a possible solution so long as there is a way to reverse a TRIM command for a specific logical block address. At which point RZAT may be equivalent in behavior to DRAT or Non-deterministic TRIM.

Ideally Artifice will be run with TRIM disabled in a similar manner to whole disk encryption schemes like [dm-crypt](http://asalor.blogspot.com/2011/08/trim-dm-crypt-problems.html).

### Multiple Snapshot Attacks.

Artifice assumes an adversary has the ability to take static snapshots of the disk. So therefore the deniability of any changes to the disk between two snapshots must be deniable. Previous system rely on random patterns of cover writes or "chaff". This is unreliable as everyday disk accesses are not guaranteed to be random.

Artifice intends to limit writes to locations that have been recently deleted by the user. This would require an additional data structure to track the status of every block in the file system to be stored in Artifice. Therefore the user can say that any changes to the free space of the file system are because of normal allocation and deletion activities.

### Deniability of Psuedorandom information in unallocated blocks.

While there is a great deal of psuedorandom information in unallocated blocks not everything on the disk is psuedorandom. In addition if a pseudorandom block is stored in the free space of a file system then an adversary should be able to restore it and the file it was part of. If the restored file was pseudorandom because it was DRM protected media, then the adversary should be able to view or play the media contained in the file. If the restored file is pseudorandom because it is encrypted then it should be decryptable into a readable file. A file that cannot be recovered in these two ways is suspicious.

There is a relatively simple way around this, hiding pseudorandom data in a file system capable of secure deletion or securely wiping a drive before using AWhen I was young, and I played all the sports, it didn't help one bit. Now that I'm old and sedentary, it has about the same severity when I'm off meds, but life is so much easier and better managed when I'm on meds.

rtifice. A secure delete file system is one where the data is stored encrypted similar to systems like dm-crypt but when it is time to delete said data, the key is simply discarded. We achieve destruction by encryption, without the key the encrypted data is irrecoverable. So the contents of all deleted information is plausibly deniable and we hypothesize is no longer suspicious as all free space is irrecoverable random information. This approach can also provide resistance to a multiple snapshot attack. This final point relies on the existence of a log structured secure delete file system.

## TODO

- [ ] `64 bit` block pointer support and `64 bit` EXT4, currently only 32 bit.
- [ ] Information Dispersal for metadata blocks over encryption and replication.
- [ ] In house library for encryption and cryptographic hashing (avoid the kernel crypto API to minimize reliance on Linux specific code).
- [ ] Persistant data structure to track the free space to be saved to the disk alongside metadata, useful for multiple snapshot defence.
- [ ] NTFS support.
- [ ] APFS support.
- [ ] F2FS/YAFFS/some other log structured system.
- [ ] Verify compatibility with ARM systems (SIMD and that one bit scan reverse inline function).

## Guidelines

All code pushed to the repository is formatted using `clang-format`. Style guide parameters are as follows:

```
{
    BasedOnStyle: WebKit,
    AlignTrailingComments: true,
    AllowShortIfStatementsOnASingleLine: true,
    AlwaysBreakAfterDefinitionReturnType: true,
    PointerAlignment: Right,
    ForEachMacros: ['list_for_each_entry', 'list_for_each_entry_safe', 'bio_for_each_segment']
}
```
Add 'ForEachMacros' definitions as required in the code.

Code which has not been formatted with these options will be rejected for a merge.

