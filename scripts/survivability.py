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
def prob_survival_sss(k, m, p):
    return binom.cdf(m, k+m, p)

def prob_survival_rs(e, d, m, p):
    return binom.cdf(m-d, e+m, p)

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
        print("|---Metadata size for Reed-Solomon-----|")
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
        print("")

    return metadata_size

#Assumes that there is only 1 data block per code word.
#
def calc_metadata_size_shamir(blocks, shares, threshold, replicas, verbose):
    pointer_size = 4
    small_checksum = 2
    art_block_hash = 16

    amplification_factor = shares
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (shares * carrier_block_tuple) + art_block_hash
    pointers_per_pointerblock = (block_size / pointer_size) - 1

    entries_per_block = math.floor(block_size / record_size)
    num_map_blocks = math.ceil(blocks / entries_per_block)
    num_map_map_blocks = math.ceil(num_map_blocks / entries_per_block)
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    metadata_overhead = (metadata_size / blocks) * 100
    effective_artifice_size = (art_size_blocks * amplification_factor) + metadata_size
   
    if verbose == True:
        print("|---Metadata size for Secret Sharing-----|")
        print("Codeword configuration: reconstruct threshold {}, {} shares".format(threshold, shares))
        print("Write amplification factor: {}".format(shares))
        print("Record Size: {} bytes".format(record_size))
        print("Entries per map block: {}".format(entries_per_block))
        print("Number of map blocks: {}".format(num_map_blocks))
        print("Number of map map blocks: {}".format(num_map_map_blocks))
        print("Number of pointer bocks: {}".format(num_pointer_blocks))
        print("Metadata size in blocks: {}".format(metadata_size))
        print("Metadata overhead: {} percent".format(metadata_overhead))
        print("effective artifice size: {}".format(effective_artifice_size))
        print("")

    return metadata_size

def calc_total_size_rs(size, parity, data, entropy):
    return (size * (parity / data)) + calc_metadata_size_rs(size, parity, entropy, data, 8, False)

def calc_total_size_sss(size, k, m):
    return (size * (k + m)) + calc_metadata_size_shamir(size, m, k, 8, False)

#TODO both of these need some work so that we can more accurately model overwrite resistance with redundant metadata
#hard coded for 1 entropy block, multiple data blocks
def prob_metadata_alive_rs(e, d, m):
    return math.pow(prob_survival_rs(e, d, m, prob_success), (calc_metadata_size_rs(art_size_blocks, m, e, d, 8, False) * num_days))

#in this case k is the reconstruct threshold, m is the number of additional shares
def prob_metadata_alive_sss(k, m):
    return math.pow(prob_survival_sss(k, m, prob_success), (calc_metadata_size_shamir(art_size_blocks, m, k, 8, False) * num_days))

#k is data + entropy, m 
def prob_artifice_alive_rs(e, d, m):
    return math.pow(prob_survival_rs(e, d, m, prob_success), (calc_total_size_rs(art_size_blocks, m, d, e) * num_days))

#k is the reconstruct threshold, m is the number of additional shares
def prob_artifice_alive_sss(k, m):
    return math.pow(prob_survival_sss(k, m, prob_success), (calc_total_size_sss(art_size_blocks, m, k) * num_days))

def prob_nines(k, m, p):
    return -math.log10(1 - prob_survival(k, m, p))

#mean time to failure (MTTF) is in hours
#calculates the probability that a given disk will die
def prob_disk_alive(mttf, days):
    return ((1 - (1/mttf)) ** (days * 24))

def main():
    #calc_metadata_size_rs(art_size_blocks, 4, 1, 2, 8, True)
    #calc_metadata_size_shamir(art_size_blocks, 4, 1, 8, True)
    
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
    '''m_values = np.arange(1, 9, 1)
    prob1 = []
    prob2 = []
    prob3 = []
    prob4 = []
    for i in m_values:
        #1 entropy block, 1 data block, i parity blocks
        prob1.append(prob_metadata_alive_rs(1, 1, i))
        #reconstruct threshold of 2, i additional blocks
        prob2.append(prob_metadata_alive_sss(2, i))
        #1 entropy block, 2 data blocks, i parity blocks
        prob3.append(prob_metadata_alive_rs(1, 2, i))
        #reconstruct threshold of 3, i additional blocks
        prob4.append(prob_metadata_alive_sss(3, i))

    plt.xlabel("Number of Parity Blocks")
    plt.ylabel("Probability of Survival")
    plt.title("Probability of Metadata Survival vs Number of Parity Blocks")
    rs = plt.plot(m_values, prob1, label='RS, 1 data block')
    rs2 = plt.plot(m_values, prob3, label='RS, 2 data blocks')
    shamir = plt.plot(m_values, prob2, label='SSS, threshold 2')
    shamir2 = plt.plot(m_values, prob4, label='SSS, threshold 3')
    plt.legend()
    plt.show()'''


    m_values = np.arange(1, 9, 1)
    prob1 = []
    prob2 = []
    prob3 = []
    prob4 = []
    for i in m_values:
        #1 entropy block, 1 data block, i parity blocks
        prob1.append(prob_artifice_alive_rs(1, 1, i))
        #reconstruct threshold of 2, i additional blocks
        prob2.append(prob_artifice_alive_sss(2, i))
        #1 entropy block, 2 data blocks, i parity blocks
        prob3.append(prob_artifice_alive_rs(1, 2, i))
        #reconstruct threshold of 3, i additional blocks
        prob4.append(prob_artifice_alive_sss(3, i))

    plt.xlabel("Number of Parity Blocks")
    plt.ylabel("Probability of Survival")
    plt.title("Probability of Artifice Survival vs Number of Parity Blocks")
    rs = plt.plot(m_values, prob1, label='RS, 1 data block')
    rs2 = plt.plot(m_values, prob3, label='RS, 2 data blocks')
    shamir = plt.plot(m_values, prob2, label='SSS, threshold 2')
    shamir2 = plt.plot(m_values, prob4, label='SSS, threshold 3')
    plt.legend()
    plt.show()



if __name__ == "__main__":
    main()
