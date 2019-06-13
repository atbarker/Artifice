#!/usr/bin/python3
import numpy as np
import math
from scipy.stats import binom
from scipy.interpolate import interp1d
import matplotlib.pyplot as plt

block_size = 4096 #4KB
total_size = 549755813888 #512GB
num_blocks = total_size / block_size
blocks_overwritten = 1310720 #5GB in blocks
art_size_blocks = 1310720 #5GB artifice instance
prob_success = blocks_overwritten / num_blocks
num_days = 365


#in matlab this is represented as Sum[PDF[BinomialDistribution[k+m,p],i], {i, 0, m}]
#as this is just a cumulative distribution function, python can do this for us
def prob_survival(k, m, p):
    return binom.cdf(m, k+m, p)

def prob_survival_artifice(art_size, k, m, p):
    return (prob_survival(k, m, p)) ** art_size_blocks

def calc_metadata_size_rs(blocks, parity, entropy, data, replicas, verbose):
    pointer_size = 4
    small_checksum = 2
    art_block_hash = 16
    entropy_filename_hash = 8

    amplification_factor = parity / data
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (parity * carrier_block_tuple) + (entropy * pointer_size) + art_block_hash + entropy_filename_hash
    pointers_per_pointerblock = (block_size / pointer_size - 1)

    entries_per_block = math.floor(block_size / record_size)
    num_map_blocks = math.ceil((blocks / data) / entries_per_block)
    num_map_map_blocks = math.ceil((num_map_blocks / data) / entries_per_block)
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    metadata_overhead = (metadata_size / blocks) * 100
    effective_artifice_size = (art_size_blocks * amplification_factor) + metadata_size

    if verbose == True:
        print("Codeword configuration: {} data blocks, {} entropy, {} parity blocks".format(data, entropy, parity))
        print("Write amplification factor: {}".format(amplification_factor))
        print("Record Size: {} bytes".format(record_size))
        print("Entries per map block: {}".format(entries_per_block))
        print("Number of map blocks: {}".format(num_map_blocks))
        print("Number of map map blocks: {}".format(num_map_map_blocks))
        print("Number of pointer bocks: {}".format(num_pointer_blocks))
        print("Metadata size in blocks: {}".format(metadata_size))
        print("Metadata overhead: {} percent".format(metadata_overhead))
        print("effective artifice size: {}".format(effective_artifice_size))

    return metadata_size


def calc_metadata_size_shamir(blocks, parity, data, replicas, verbose):
    pointer_size = 4
    small_checksum = 2
    art_block_hash = 16

    amplification_factor = parity / data
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (parity * carrier_block_tuple) + art_block_hash
    pointers_per_pointerblock = (block_size / pointer_size) - 1

    entries_per_block = math.floor(block_size / record_size)
    num_map_blocks = math.ceil(blocks / entries_per_block)
    num_map_map_blocks = math.ceil(num_map_blocks / entries_per_block)
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    metadata_overhead = (metadata_size / blocks) * 100
    effective_artifice_size = (art_size_blocks * amplification_factor) + metadata_size
   
    if verbose == True:
        print("Codeword configuration: {} data blocks, {} parity blocks".format(data, parity))
        print("Write amplification factor: {}".format(amplification_factor))
        print("Record Size: {} bytes".format(record_size))
        print("Entries per map block: {}".format(entries_per_block))
        print("Number of map blocks: {}".format(num_map_blocks))
        print("Number of map map blocks: {}".format(num_map_map_blocks))
        print("Number of pointer bocks: {}".format(num_pointer_blocks))
        print("Metadata size in blocks: {}".format(metadata_size))
        print("Metadata overhead: {} percent".format(metadata_overhead))
        print("effective artifice size: {}".format(effective_artifice_size))

    return metadata_size

def calc_total_size(size, parity, data):
    return size * (parity / data)

#TODO both of these need some work so that we can more accurately model overwrite resistance with redundant metadata
#hard coded for 1 entropy block, multiple data blocks
def prob_metadata_alive_rs(k, m):
    return math.pow(prob_survival(k, m, prob_success), (calc_metadata_size_rs(art_size_blocks, m, 1, k-1, 8, False) * num_days))

def prob_metadata_alive_sss(k, m):
    return math.pow(prob_survival(k, m, prob_success), (calc_metadata_size_shamir(art_size_blocks, m, k, 8, False) * num_days))

def prob_artifice_alive_rs(k, m):
    return math.pow(prob_survival(k, m, prob_success), (calc_total_size(art_size_blocks, m, k, 8, False) * num_days))

def prob_artifice_alive_sss(k, m):
    return math.pow(prob_survival(k, m, prob_success), (calc_total_size(art_size_blocks, m, k, 8, False) * num_days))

def prob_nines(k, m, p):
    return -math.log10(1 - prob_survival(k, m, p))

#mean time to failure (MTTF) is in hours
def prob_disk_alive(mttf, days):
    return ((1 - (1/mttf)) ** (days * 24))

def main():
    smooth = True
    print("|---Metadata size for Reed-Solomon-----|")
    calc_metadata_size_rs(art_size_blocks, 4, 1, 2, 8, True)
    print(" ")
    print("|---Metadata size for secret sharing---|")
    calc_metadata_size_shamir(art_size_blocks, 4, 1, 8, True)
    
    #nines
    '''m_max = 0.05
    m_values = np.arange(0.0001, 0.05, 0.0001)
    prob1 = []
    prob2 = []
    prob3 = []
    prob4 = []
    prob5 = []
    for i in m_values:
        prob1.append(prob_nines(3, 3, i))
        prob2.append(prob_nines(3, 4, i))
        prob3.append(prob_nines(3, 5, i))
        prob4.append(prob_nines(3, 6, i))
        prob5.append(prob_nines(3, 7, i))
    plt.axis([0, 0.05, 0, 14])
    plt.plot(m_values, prob1, m_values, prob2, m_values, prob3, m_values, prob4, m_values, prob5)
    plt.show()'''

    #probability of survival for metadata, reed solomon
    m_values = np.arange(0, 9, 1)
    prob1 = []
    prob2 = []
    prob3 = []
    for i in m_values:
        prob1.append(prob_metadata_alive_rs(2, i))
        prob2.append(prob_metadata_alive_sss(1, i))
        prob3.append(prob_metadata_alive_rs(3, i))

    plt.xlabel("Number of Parity Blocks")
    plt.ylabel("Probability of Survival")
    plt.title("Probability of Survival vs Number of Parity Blocks")
    rs = plt.plot(m_values, prob1, label='RS, 1 data block')
    rs2 = plt.plot(m_values, prob3, label='RS, 2 data blocks')
    shamir = plt.plot(m_values, prob2, label='secret sharing')
    plt.legend()
    plt.show()



if __name__ == "__main__":
    main()
