#!/usr/bin/python3
import numpy as np
import math
from scipy.stats import binom
import matplotlib.pyplot as plt

block_size = 4096 #4KB
total_size = 54975581388 #512GB
num_blocks = total_size / block_size
blocks_overwritten = 1310720 #5GB in blocks
mat_size_blocks = 1310720 #5GB artifice instance
prob_success = blocks_overwritten / num_blocks
num_days = 365


#in matlab this is represented as Sum[PDF[BinomialDistribution[k+m,p],i], {i, 0, m}]
#as this is just a cumulative distribution function, python can do this for us
def prob_survival(k, m, p):
    return binom.cdf(m, k+m, p)

def prob_survival_artifice(art_size, k, m, p):
    return (prob_survival(k, m, p)) ** art_size

def calc_metadata_size_rs(blocks, parity, entropy, data):
    #all measurements are in bytes
    pointer_size = 4
    small_checksum = 2
    art_block_hash = 16
    entropy_filename_hash = 8
    superblock_replicas = 8

    amplification_factor = parity / data
    print("amplification factor: {}".format(amplification_factor))
    carrier_block_tuple = pointer_size + small_checksum
    #I forget what the 4 is supposed to do, probably to align everything to an easy block boundary
    record_size = ((entropy + parity) * carrier_block_tuple) + art_block_hash + entropy_filename_hash + 4
    print("record size: {}".format(record_size))
    pointers_per_pointerblock = (block_size / pointer_size - 1)

    entries_per_block = math.floor(block_size / record_size)
    print("entries per block: {}".format(entries_per_block))
    num_map_blocks = math.ceil((blocks * amplification_factor) / entries_per_block)
    print("number of map blocks: {}".format(num_map_blocks))
    num_map_map_blocks = math.ceil((num_map_blocks * amplification_factor) / entries_per_block)
    print("number of map map blocks: {}".format(num_map_map_blocks))
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    print("number of pointer_blocks: {}".format(num_pointer_blocks))
    total_size = ((num_pointer_blocks + 1) * superblock_replicas) + num_map_map_blocks + num_map_blocks
    print("total size in blocks: {}".format(total_size))
    return total_size


def calc_metadata_size_shamir(blocks, parity, data):
    #all measurements are in bytes
    pointer_size = 4
    small_checksum = 2
    art_block_hash = 16
    superblock_replicas = 8

    amplification_factor = parity / data
    print("amplification factor: {}".format(amplification_factor))
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (parity * carrier_block_tuple) + art_block_hash
    print("record size: {}".format(record_size))
    pointers_per_pointerblock = (block_size / pointer_size) - 1

    entries_per_block = math.floor(block_size / record_size)
    print("entries per block: {}".format(entries_per_block))
    num_map_blocks = math.ceil(blocks / entries_per_block)
    print("number of map blocks: {}".format(num_map_blocks))
    num_map_map_blocks = math.ceil(num_map_blocks / entries_per_block)
    print("number of map map blocks: {}".format(num_map_map_blocks))
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    print("number of pointer_blocks: {}".format(num_pointer_blocks))
    total_size = ((num_pointer_blocks + 1) * superblock_replicas) + num_map_map_blocks + num_map_blocks
    print("total size in blocks: {}".format(total_size))
    return total_size

def calc_total_size(size, parity, data):
    return size * (parity / data)

def prob_metadata_alive(k, m):
    return (prob_survival(k, m, ps) ** (calc_metadata_size_rs(mat_size_blocks, m, 2, 2) * num_days))

def prob_artifice_alive(k, m):
    return (prob_survival(k, m, ps) ** (calc_total_size(mat_size_blocks, m, k) * num_days))

#mean time to failure (MTTF) is in hours
def prob_disk_alive(mttf, days):
    return ((1 - (1/mttf)) ** (days * 24))

def main():
    print("Metadata_size for Reed-Solomon")
    calc_metadata_size_rs(mat_size_blocks, 4, 2, 2)
    print(" ")
    print("Metadata size for shamir secret sharing")
    calc_metadata_size_shamir(mat_size_blocks, 4, 1)

if __name__ == "__main__":
    main()
