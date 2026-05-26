# LF-Filter

LF-Filter is a compact data structure for Intra-Flow Packet Delay Monitoring.

We test the accuracy of LF-Filter with the code in `src/sketch/LFFilter.hh`.

The MAVI dataset used in the experiment can be downloaded from the following link: https://mawi.wide.ad.jp/mawi/



To compile the code, run the following instructions:

```shell
mkdir build
cmake ..
make
./main
```

The configuration is written in `config.ini`.
