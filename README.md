# FD-Filter

FD-Filter is a compact data structure for Intra-Flow Packet Delay Monitoring.



We test the accuracy of FD-Filter with the code in `src/sketch/FDFilter.hh` and `src/sketch/MergeableFDFilter.hh`.

The Counter Combination optimization is implemented in the files starting with 'Opt'.

The Lazy Aging optimization is implemented in the files starting with 'GAging'.



To compile the code, run the following instructions:

```shell
mkdir build
cmake ..
make
./main
```

The configuration is written in `config.ini`.