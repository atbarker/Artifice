"""
Script used to calculate different overhead consumption
based on different configurations of a artifice file
system. 

Author: Yash Gupta <ygupta@ucsc.edu>
"""

class afs_config:
    def __init__(self, tuple_shards=4, shards=4, afs_bits=32, blk_bits=12, entropy_sz=8):
        ## Entry in the map.
        tuple_sz = 2 + 4 + 4
        data_hash_sz = 16
        map_entry_sz = (tuple_shards * tuple_sz) + data_hash_sz + entropy_sz
        print "map_entry_sz: " + str(map_entry_sz) + " bytes"

        ## Entire map.
        num_blocks = 2 ** (afs_bits - blk_bits)
        map_sz = map_entry_sz * num_blocks
        map_sz *= shards

        ## Map Block overhead.
        entries_in_block = (2 ** blk_bits) / map_entry_sz
        entries_in_map_block = 1023 * entries_in_block
        num_map_blocks = num_blocks / entries_in_map_block
        map_blocks_sz = num_map_blocks * 4096
        map_blocks_sz *= shards
        print "map_blocks_sz: " + str(((float(map_blocks_sz) / 1024) / 1024)) + " MB"
        print

        ## Overhead.
        afs_sz = 2 ** afs_bits
        self.overhead = (float(map_sz + map_blocks_sz) / float(afs_sz)) * 100.0

    def get_overhead(self):
        return self.overhead

default = afs_config()
higher_shards = afs_config(shards = 8)
higher_tuple_shards = afs_config(tuple_shards=8)
high = afs_config(shards=8, tuple_shards=8)
sectors = afs_config(blk_bits=9)
entropy_filename = afs_config(entropy_sz=32)

print "Default        :", str(default.get_overhead()), "%"
print "High Shards (8):", str(higher_shards.get_overhead()), "%"
print "Tuple Shards(8):", str(higher_tuple_shards.get_overhead()), "%"
print "High        (8):", str(high.get_overhead()), "%"
print "Sectors   (512):", str(sectors.get_overhead()), "%"
print "Entropy    (32):", str(entropy_filename.get_overhead()), "%"