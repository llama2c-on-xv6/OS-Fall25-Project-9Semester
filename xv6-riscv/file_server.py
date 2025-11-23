#!/usr/bin/env python3
"""
RFTP File Server for xv6 LLM Weight Transfer
Place this file in your repository root (not inside xv6-riscv/)
Usage: python3 file_server.py [port]
"""

import socket
import struct
import hashlib
import sys
import os
from pathlib import Path

# Protocol Constants
RFTP_VERSION = 1
RFTP_PORT = 25999
RFTP_MAX_PAYLOAD = 512

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

# Error Codes
ERR_NONE = 0x00
ERR_INVALID_FILE = 0x01
ERR_INVALID_PACKET = 0x02
ERR_SERVER_BUSY = 0x03

class FileEntry:
    def __init__(self, path, file_id):
        self.file_id = file_id
        self.path = path
        self.data = None
        self.size = 0
        self.packet_count = 0
        self.sha256_hash = None
        
    def load(self):
        """Load file into memory and compute metadata."""
        print(f"Loading {self.path}...")
        with open(self.path, 'rb') as f:
            self.data = f.read()
        self.size = len(self.data)
        self.packet_count = (self.size + RFTP_MAX_PAYLOAD - 1) // RFTP_MAX_PAYLOAD
        self.sha256_hash = hashlib.sha256(self.data).digest()
        print(f"  Size: {self.size} bytes ({self.packet_count} packets)")
        print(f"  SHA-256: {self.sha256_hash.hex()}")

class RFTPServer:
    def __init__(self, port=RFTP_PORT):
        self.port = port
        self.sock = None
        self.files = {}
        
    def load_files(self, model_path, tokenizer_path):
        """Load model and tokenizer files into memory."""
        # Load model weights
        if os.path.exists(model_path):
            self.files[FILE_ID_MODEL] = FileEntry(model_path, FILE_ID_MODEL)
            self.files[FILE_ID_MODEL].load()
        else:
            print(f"WARNING: Model file not found: {model_path}")
            
        # Load tokenizer
        if os.path.exists(tokenizer_path):
            self.files[FILE_ID_TOKENIZER] = FileEntry(tokenizer_path, FILE_ID_TOKENIZER)
            self.files[FILE_ID_TOKENIZER].load()
        else:
            print(f"WARNING: Tokenizer file not found: {tokenizer_path}")
    
    def compute_xor_checksum(self, data):
        """Compute 16-bit XOR checksum."""
        checksum = 0
        for i in range(0, len(data), 2):
            if i + 1 < len(data):
                word = data[i] | (data[i + 1] << 8)
            else:
                word = data[i]
            checksum ^= word
        return checksum & 0xFFFF
    
    def make_header(self, msg_type, seq_num):
        """Create 4-byte RFTP header."""
        return struct.pack('!BBH', RFTP_VERSION, msg_type, seq_num)
    
    def handle_metadata_request(self, data, addr):
        """Handle METADATA_REQUEST message."""
        if len(data) < 5:
            return None
        _, _, seq_num, file_id = struct.unpack('!BBHB', data[:5])
        
        print(f"[{addr}] METADATA_REQUEST: file_id={file_id}, seq={seq_num}")
        
        if file_id not in self.files:
            # Send error response
            resp = self.make_header(MSG_ERROR, seq_num)
            resp += struct.pack('!BB', ERR_INVALID_FILE, file_id)
            return resp
        
        f = self.files[file_id]
        # Build METADATA_RESPONSE: header(4) + file_id(1) + err(1) + size(4) + pkt_cnt(4) + hash(32)
        resp = self.make_header(MSG_METADATA_RESP, seq_num)
        resp += struct.pack('!BBII', file_id, ERR_NONE, f.size, f.packet_count)
        resp += f.sha256_hash
        
        print(f"  -> METADATA_RESPONSE: size={f.size}, packets={f.packet_count}")
        return resp
    
    def handle_data_request(self, data, addr):
        """Handle DATA_REQUEST message."""
        if len(data) < 9:
            return None
        _, _, seq_num, file_id, pkt_idx = struct.unpack('!BBHBI', data[:9])
        
        return self.make_data_response(file_id, pkt_idx, seq_num, addr)
    
    def handle_batch_request(self, data, addr):
        """Handle BATCH_REQUEST message."""
        if len(data) < 6:
            return None
        _, _, seq_num, file_id, count = struct.unpack('!BBHBB', data[:6])
        
        if count > 16:
            count = 16
            
        print(f"[{addr}] BATCH_REQUEST: file_id={file_id}, count={count}, seq={seq_num}")
        
        responses = []
        for i in range(count):
            offset = 6 + i * 4
            if offset + 4 > len(data):
                break
            pkt_idx = struct.unpack('!I', data[offset:offset+4])[0]
            resp = self.make_data_response(file_id, pkt_idx, seq_num + i, addr, quiet=True)
            if resp:
                responses.append(resp)
        
        print(f"  -> Sending {len(responses)} DATA_RESPONSEs")
        return responses
    
    def make_data_response(self, file_id, pkt_idx, seq_num, addr, quiet=False):
        """Create DATA_RESPONSE for a specific packet."""
        if file_id not in self.files:
            resp = self.make_header(MSG_ERROR, seq_num)
            resp += struct.pack('!BB', ERR_INVALID_FILE, file_id)
            return resp
        
        f = self.files[file_id]
        
        if pkt_idx >= f.packet_count:
            resp = self.make_header(MSG_ERROR, seq_num)
            resp += struct.pack('!BB', ERR_INVALID_PACKET, pkt_idx & 0xFF)
            return resp
        
        # Extract payload
        start = pkt_idx * RFTP_MAX_PAYLOAD
        end = min(start + RFTP_MAX_PAYLOAD, f.size)
        payload = f.data[start:end]
        payload_len = len(payload)
        checksum = self.compute_xor_checksum(payload)
        
        if not quiet:
            print(f"[{addr}] DATA_REQUEST: file_id={file_id}, pkt={pkt_idx}")
            print(f"  -> DATA_RESPONSE: {payload_len} bytes, checksum=0x{checksum:04x}")
        
        # Build DATA_RESPONSE
        resp = self.make_header(MSG_DATA_RESP, seq_num)
        resp += struct.pack('!BBIHH', file_id, ERR_NONE, pkt_idx, payload_len, checksum)
        resp += payload
        
        return resp
    
    def run(self):
        """Main server loop."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0', self.port))
        print(f"\nRFTP Server listening on port {self.port}")
        print("Waiting for xv6 client connections...\n")
        
        while True:
            try:
                data, addr = self.sock.recvfrom(1024)
                if len(data) < 4:
                    continue
                    
                version, msg_type = struct.unpack('!BB', data[:2])
                
                if version != RFTP_VERSION:
                    print(f"[{addr}] Invalid version: {version}")
                    continue
                
                response = None
                
                if msg_type == MSG_METADATA_REQ:
                    response = self.handle_metadata_request(data, addr)
                elif msg_type == MSG_DATA_REQ:
                    response = self.handle_data_request(data, addr)
                elif msg_type == MSG_BATCH_REQ:
                    response = self.handle_batch_request(data, addr)
                else:
                    print(f"[{addr}] Unknown message type: 0x{msg_type:02x}")
                
                if response:
                    if isinstance(response, list):
                        for r in response:
                            self.sock.sendto(r, addr)
                    else:
                        self.sock.sendto(response, addr)
                        
            except KeyboardInterrupt:
                print("\nShutting down server...")
                break
            except Exception as e:
                print(f"Error: {e}")
                
        self.sock.close()

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else RFTP_PORT
    
    # Default file paths - adjust these for your setup
    model_path = "../llama2c/stories15M.bin"
    tokenizer_path = "../llama2c/tokenizer.bin"
    
    # Alternative paths to check
    alt_paths = [
        ("stories15M.bin", "tokenizer.bin"),
        ("../stories15M.bin", "../tokenizer.bin"),
        ("llama2c/stories15M.bin", "llama2c/tokenizer.bin"),
    ]
    
    for mp, tp in alt_paths:
        if os.path.exists(mp):
            model_path = mp
        if os.path.exists(tp):
            tokenizer_path = tp
    
    server = RFTPServer(port)
    server.load_files(model_path, tokenizer_path)
    
    if not server.files:
        print("ERROR: No files loaded! Please check file paths.")
        print("Expected: stories15M.bin and tokenizer.bin")
        sys.exit(1)
    
    server.run()

if __name__ == "__main__":
    main()