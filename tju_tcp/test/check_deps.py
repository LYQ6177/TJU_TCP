#!/usr/bin/env python3
def main():
    import matplotlib
    import numpy
    print('matplotlib', matplotlib.__version__)
    try:
        from fabric import Connection
        print('fabric ok')
    except Exception as e:
        print('fabric missing', e)
    try:
        from scapy.all import rdpcap
        print('scapy ok')
    except Exception as e:
        print('scapy missing', e)

if __name__ == '__main__':
    main()
