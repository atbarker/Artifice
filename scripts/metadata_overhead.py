"""
Script used to calculate different overhead consumption
based on different configurations of a matryoshka file
system. 

Author: Yash Gupta <ygupta@ucsc.edu>
"""
import math

def roundup_to_p2(number):
    """
    Round up to nearest power of two using
    a simple check to see if a number is already
    a power of two or not.
    """

    if number & (number-1) == 0:
        return number
        
    number *= 2
    return 2 ** long(math.log(number, 2))

class mks_config:
    """
    Default Values:
    shards        = 8   (8 blocks due to secret splitting)
    checksum_bits = 16  (2 byte checksums)
    hash_bits     = 128 (16 Bytes)
    mks_bits      = 32  (4 GB instance)
    disk_bits     = 40  (1 TB storage)
    blk_bits      = 12  (4 KB block)
    """

    def __init__(self, shards=8, checksum_bits=0, hash_bits=128, mks_bits=32, disk_bits=40, blk_bits=12):
        self.shards = shards
        self.checksum_bits = checksum_bits
        self.hash_bits = hash_bits
        self.mks_bits = mks_bits
        self.disk_bits = disk_bits
        self.blk_bits = blk_bits

        ## Sizes in bytes due to bits
        self.checksum_sz = checksum_bits / 8
        self.hash_sz = hash_bits / 8
        self.blk_sz = 2 ** blk_bits
        self.mks_sz = 2 ** mks_bits
        self.disk_sz = 2 ** disk_bits

        ## Number of blocks (and bits required to represent them)
        ## for matryoshka instance and the disk.
        self.mks_blks = self.mks_sz / self.blk_sz
        self.mks_blks_bits = int(math.log(self.mks_blks, 2))

        self.disk_blks = self.disk_sz / self.blk_sz
        self.disk_blks_bits = int(math.log(self.disk_blks, 2))

        ## Sizes for the Matryoshka Map
        self.mks_map_diskblk_sz = roundup_to_p2(self.disk_blks_bits) / 8
        self.mks_map_entry_sz = (self.shards * self.checksum_sz) + (self.shards * self.mks_map_diskblk_sz) + self.hash_sz
        self.mks_map_sz = self.mks_blks * self.mks_map_entry_sz

        self.overhead = ((float(self.mks_map_sz) / float(self.mks_sz)) * 100) * self.shards

    def get_overhead(self):
        return self.overhead

default = mks_config()
lower_ss4 = mks_config(shards = 4)
lower_ss6 = mks_config(shards = 6)
bigger_mks5 = mks_config(mks_bits=33)
sha1 = mks_config(hash_bits = 160)
bigger_disk = mks_config(disk_bits = 43)

print "Default        :", str(default.get_overhead()), "%"
print "Less Shards (4):", str(lower_ss4.get_overhead()), "%"
print "Less Shards (6):", str(lower_ss6.get_overhead()), "%"
print "Bigger MKS (33):", str(bigger_mks5.get_overhead()), "%"
print "SHA1 (20 Bytes):", str(sha1.get_overhead()), "%"
print "Big Disk  (8TB):", str(bigger_disk.get_overhead()), "%"