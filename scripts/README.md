# Artifice supporting scripts

## Metadata Overhead Calculator

Please contact Yash for more information about this script.

## Survivability

This script handles the theoretical survivability models for Artifice. It will calculate probabilities of survival for an Artifice instance based on the size of the instance, type of encoding, rate of overwrite, and layout of the Artifice code word. Right now it will generate graphs for the probability of survival for either the metadata or the entire instance with datasets for both Shamir Secret Sharing and Reed-Solomon + Entropy.

There are three command line options, nines, metadata, all, and disk. 

    The 'nines' option simply prints out a graph generated from the binomial CDF that represents the probability of overwrite without taking into account the size of the Artifice instance.
    The 'metadata' option graphs the probability of survival for only the metadata. Since if the metadata survives then we can at the very least recognize errors in the rest of the instance.
    The 'all' option  graphs the probability of survival for the entire Artifice instance.
    The 'disk' option calculates the probability of survival for a disk given the mean time to failure. 

## Benchmarking

The 'bench' folder contains scripts for benchmarking Artifice.
