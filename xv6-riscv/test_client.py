#!/usr/bin/env python3
"""
RFTP Test Client - Python version
Use this to verify your server works before testing with xv6

Usage: python3 test_client.py [server_ip] [port]
"""

import socket
import struct
import hashlib
import sys
import time

# Protocol Constants
RFTP_VERSION = 1
RFTP_PORT = 9999
RFTP_MAX_PAYLOAD = 512
RFTP_BATCH_SIZE = 16

# Message Types
MSG_METADATA_REQ = 0x01
MSG_METADATA_RESP = 0x02
MSG_DATA_REQ = 0x03
MSG_DATA_RESP = 0x04
MSG_BATCH_REQ = 0x05
MSG_ERROR = 0xFF

# File IDs
FILE_ID_MODEL = 0x01
FILE_ID_TOKENIZER = 0x02

class RFTPClient:
    def __init__(self, server_ip='127.0.0.1', port=RFTP_PORT):
        self.server = (server_ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(2.0)
        self.seq = 0
        
    def make_header(self, msg_type):
        hdr = struct.pack('!BBH', RFTP_VERSION, msg_type, self.seq)
        self.seq += 1
        return hdr
    
    def request_metadata(self, file_id):
        """Request file metadata."""
        msg = self.make_header(MSG_METADATA_REQ)
        msg += struct.pack('!B', file_id)
        
        self.sock.sendto(msg, self.server)
        
        data, _ = self.sock.recvfrom(1024)
        
        if len(data) < 46:
            raise Exception(f"Invalid metadata response: {len(data)} bytes")
        
        ver, mtype, seq, fid, err, size, pkt_cnt = struct.unpack('!BBHBBII', data[:14])
        sha256_hash = data[14:46]
        
        if mtype == MSG_ERROR:
            raise Exception(f"Server error: {err}")
            
        return {
            'file_id': fid,
            'error': err,
            'size': size,
            'packet_count': pkt_cnt,
            'sha256': sha256_hash
        }
    
    def request_batch(self, file_id, indices):
        """Request multiple packets."""
        msg = self.make_header(MSG_BATCH_REQ)
        msg += struct.pack('!BB', file_id, len(indices))
        for idx in indices:
            msg += struct.pack('!I', idx)
        
        self.sock.sendto(msg, self.server)
    
    def receive_data_packet(self):
        """Receive a single data packet."""
        try:
            data, _ = self.sock.recvfrom(1024)
        except socket.timeout:
            return None
            
        if len(data) < 14:
            return None
            
        ver, mtype, seq, fid, err, idx, plen, csum = struct.unpack('!BBHBBIHH', data[:14])
        
        if mtype != MSG_DATA_RESP:
            return None
            
        payload = data[14:14+plen]
        
        # Verify checksum
        computed = 0
        for i in range(0, len(payload), 2):
            word = payload[i]
            if i + 1 < len(payload):
                word |= payload[i+1] << 8
            computed ^= word
        computed &= 0xFFFF
        
        if computed != csum:
            print(f"  Checksum mismatch for packet {idx}")
            return None
            
        return {
            'index': idx,
            'payload': payload,
            'length': plen
        }
    
    def fetch_file(self, file_id, simulate_loss=0.0):
        """Fetch a complete file."""
        import random
        
        file_name = "model" if file_id == FILE_ID_MODEL else "tokenizer"
        print(f"\n{'='*50}")
        print(f"Fetching {file_name} (file_id={file_id})")
        print(f"{'='*50}")
        
        # Step 1: Get metadata
        print("\n[1] Requesting metadata...")
        meta = self.request_metadata(file_id)
        print(f"    File size: {meta['size']:,} bytes")
        print(f"    Packets: {meta['packet_count']}")
        print(f"    SHA-256: {meta['sha256'].hex()}")
        
        # Step 2: Allocate buffer
        file_buffer = bytearray(meta['size'])
        received = [False] * meta['packet_count']
        
        # Step 3: Request packets in batches
        print(f"\n[2] Downloading packets...")
        start_time = time.time()
        total_batches = (meta['packet_count'] + RFTP_BATCH_SIZE - 1) // RFTP_BATCH_SIZE
        
        retries = 0
        max_retries = 5
        
        while not all(received) and retries < max_retries:
            # Find missing packets
            missing = [i for i, r in enumerate(received) if not r]
            
            if retries > 0:
                print(f"    Retry {retries}: {len(missing)} packets missing")
            
            # Request in batches
            for batch_start in range(0, len(missing), RFTP_BATCH_SIZE):
                batch = missing[batch_start:batch_start + RFTP_BATCH_SIZE]
                self.request_batch(file_id, batch)
                
                # Receive responses
                recv_count = 0
                timeout_count = 0
                while recv_count < len(batch) and timeout_count < 3:
                    pkt = self.receive_data_packet()
                    if pkt:
                        # Simulate packet loss for testing
                        if simulate_loss > 0 and random.random() < simulate_loss:
                            continue
                            
                        idx = pkt['index']
                        if not received[idx]:
                            offset = idx * RFTP_MAX_PAYLOAD
                            file_buffer[offset:offset + pkt['length']] = pkt['payload']
                            received[idx] = True
                            recv_count += 1
                    else:
                        timeout_count += 1
                
                # Progress
                done = sum(received)
                pct = done * 100 // meta['packet_count']
                print(f"\r    Progress: {pct}% ({done}/{meta['packet_count']} packets)", end='')
            
            retries += 1 if not all(received) else 0
        
        elapsed = time.time() - start_time
        print(f"\n    Downloaded in {elapsed:.2f}s ({meta['size']/elapsed/1024:.1f} KB/s)")
        
        # Step 4: Verify SHA-256
        print(f"\n[3] Verifying SHA-256...")
        computed = hashlib.sha256(bytes(file_buffer)).digest()
        print(f"    Expected: {meta['sha256'].hex()}")
        print(f"    Computed: {computed.hex()}")
        
        if computed == meta['sha256']:
            print(f"\n✓ SUCCESS! File integrity verified.")
            return bytes(file_buffer)
        else:
            print(f"\n✗ FAILED! SHA-256 mismatch.")
            return None
    
    def close(self):
        self.sock.close()

def main():
    server_ip = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
    port = int(sys.argv[2]) if len(sys.argv) > 2 else RFTP_PORT
    
    print(f"RFTP Test Client connecting to {server_ip}:{port}")
    
    client = RFTPClient(server_ip, port)
    
    try:
        # Test 1: Fetch tokenizer (smaller)
        tok_data = client.fetch_file(FILE_ID_TOKENIZER)
        if tok_data:
            print(f"Tokenizer: {len(tok_data):,} bytes ✓")
        
        # Test 2: Fetch model weights (larger)
        # Uncomment to test full model (takes longer)
        # model_data = client.fetch_file(FILE_ID_MODEL)
        # if model_data:
        #     print(f"Model: {len(model_data):,} bytes ✓")
        
        # Test 3: Simulate packet loss (10%)
        print("\n\n=== Testing with 10% simulated packet loss ===")
        tok_data2 = client.fetch_file(FILE_ID_TOKENIZER, simulate_loss=0.1)
        if tok_data2:
            print(f"Tokenizer (with loss): {len(tok_data2):,} bytes ✓")
            
    except Exception as e:
        print(f"Error: {e}")
    finally:
        client.close()

if __name__ == "__main__":
    main()