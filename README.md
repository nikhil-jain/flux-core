Flux
====

0. Building the code

  # Uncomment the appropriate MPIMOD setting in zmq-broker/Makefile
  # for your system
  make

1. Launching the framework on Hype or IPA

Generate some CURVE keys for yourself (they go in your ~/.flux directory):

  cd zmq-broker
  ./flux keygen

Use the ./scripts/launch script to start the framework in a slurm job.
It will work with or without a pre-existing slurm allocation.

  ./scripts/launch [-c NAME] [-n nnodes] [-p partition]

The -c option selects a pepe config other than the default (flat).
The -n option specifies the number of nodes, and -p the partition.
If running inside a slurm allocation, you can skip these options.

There are several pepe config files for different test topologies.

  degenerate - k=1 tree
  flat - k=N tree
  binary - k=2 tree
  trinary - k=3 tree

Ex: ipa:
  ./scripts/launch -c binary -n 2 -p pall

Ex: hype:
  ./scripts/launch -c trinary -n 64 -p pbatch

2. Testing your Flux envrionment

Once you've launched your environment, you'll be in a shell on its node 0.
You can snoop on the traffic going through node 0's cmbd using

  ./flux snoop

If you don't see anything, perhaps you didn't generate a CURVE key above?
That error condition isn't currently handled well, however you might see
something indicative of this in ./cmbd.log.

To verify that your nodes are up, run

  ./flux up

To run an MPI job launched with Slurm but using our KVS for MPI bootstrapping,
you can run

  make hello

If you get a bunch of MPI errors, perhaps you uncommented the wrong MPIMOD
above?

To ping the kvs service on rank 60

  ./flux ping '60!kvs'

3. Alternate single-node environment

An alternate way to launch a 3 node session on a single node is:

  ./scripts/screen-3

You can run some KVS unit tests against this environment with

  cd test
  make check3




