#!/usr/bin/env python3
"""
UDP file server implementing a simple request-response protocol.

Usage:
  python3 file_server.py --port 25999 --model llama2c/stories15M.bin --tokenizer llama2c/tokenizer.bin --chunk-size 1024
"""
import argparse
import socket
import struct
import hashlib
import os
import sys
import datetime

# Message types
METADATA_REQUEST = 1
METADATA_RESPONSE = 2
CHUNK_REQUEST = 3
CHUNK_RESPONSE = 4

# File IDs
FILE_MODEL = 1
FILE_TOKENIZER = 2

DEFAULT_PORT = 25999
DEFAULT_CHUNK_SIZE = 1024
MAX_UDP_PAYLOAD = 65507  # safe UDP payload maximum

def now():
    return datetime.datetime.utcnow().isoformat() + 'Z'

class ServedFile:
    def __init__(self, path, file_id, chunk_size):
        self.path = path
        self.file_id = file_id
        self.chunk_size = chunk_size
        self.load()

    def load(self):
        if not os.path.exists(self.path):
            raise FileNotFoundError(self.path)
        with open(self.path, 'rb') as f:
            self.data = f.read()
        self.filesize = len(self.data)
        self.chunk_count = (self.filesize + self.chunk_size - 1) // self.chunk_size
        self.sha256 = hashlib.sha256(self.data).digest()

    def get_chunk(self, idx):
        if idx < 0 or idx >= self.chunk_count:
            return None
        start = idx * self.chunk_size
        end = min(start + self.chunk_size, self.filesize)
        return self.data[start:end]

def pack_metadata_response(file_id, status, filesize, chunk_size, chunk_count, sha256):
    # '!B B B I H I 32s' -> msg_type(1), file_id(1), status(1), filesize(4), chunk_size(2), chunk_count(4), sha256(32)
    return struct.pack('!B B B I H I 32s', METADATA_RESPONSE, file_id, status, filesize, chunk_size, chunk_count, sha256)

def unpack_metadata_request(data):
    # Expect: [type (1)] [file_id (1)]
    if len(data) < 2:
        raise ValueError('metadata request too short')
    _, file_id = struct.unpack('!B B', data[:2])
    return file_id

def pack_chunk_response(file_id, chunk_index, data_bytes):
    data_len = len(data_bytes)
    header = struct.pack('!B B I H', CHUNK_RESPONSE, file_id, chunk_index, data_len)
    packet = header + data_bytes
    if len(packet) > MAX_UDP_PAYLOAD:
        raise ValueError(f'Packet too large to send over UDP: {len(packet)} bytes')
    return packet

def unpack_chunk_request(data):
    # Expect: [type(1)][file_id(1)][chunk_index(4)] = total 6 bytes
    if len(data) < 6:
        raise ValueError('chunk request too short')
    _, file_id = struct.unpack('!B B', data[:2])
    chunk_index = struct.unpack('!I', data[2:6])[0]
    return file_id, chunk_index

def log(msg):
    print(f'[{now()}] {msg}', flush=True)

def handle_packet(sock, data, addr, files_map):
    if not data:
        log(f'Empty packet from {addr}')
        return
    msg_type = data[0]
    try:
        if msg_type == METADATA_REQUEST:
            try:
                file_id = unpack_metadata_request(data)
            except ValueError as e:
                log(f'[REQ] MALFORMED METADATA_REQUEST from {addr}: {e}')
                return
            log(f'[REQ] METADATA_REQUEST file_id={file_id} from {addr}')
            sf = files_map.get(file_id)
            if not sf:
                resp = pack_metadata_response(file_id, 1, 0, 0, 0, b'\x00' * 32)
                sock.sendto(resp, addr)
                log(f'[RESP] METADATA_RESPONSE ERR file_id={file_id} to {addr}')
                return
            resp = pack_metadata_response(file_id, 0, sf.filesize, sf.chunk_size, sf.chunk_count, sf.sha256)
            sock.sendto(resp, addr)
            log(f'[RESP] METADATA_RESPONSE OK file_id={file_id} filesize={sf.filesize} chunks={sf.chunk_count} to {addr}')

        elif msg_type == CHUNK_REQUEST:
            try:
                file_id, chunk_index = unpack_chunk_request(data)
            except ValueError as e:
                log(f'[REQ] MALFORMED CHUNK_REQUEST from {addr}: {e}')
                return
            log(f'[REQ] CHUNK_REQUEST file_id={file_id} index={chunk_index} from {addr}')
            sf = files_map.get(file_id)
            if not sf:
                resp = pack_chunk_response(file_id, chunk_index, b'')
                sock.sendto(resp, addr)
                log(f'[RESP] CHUNK_RESPONSE ERR (no file) file_id={file_id} index={chunk_index} to {addr}')
                return
            chunk = sf.get_chunk(chunk_index)
            if chunk is None:
                resp = pack_chunk_response(file_id, chunk_index, b'')
                sock.sendto(resp, addr)
                log(f'[RESP] CHUNK_RESPONSE ERR (bad index) file_id={file_id} index={chunk_index} to {addr}')
                return
            resp = pack_chunk_response(file_id, chunk_index, chunk)
            sock.sendto(resp, addr)
            log(f'[RESP] CHUNK_RESPONSE OK file_id={file_id} index={chunk_index} len={len(chunk)} to {addr}')

        else:
            log(f'Unknown message type {msg_type} from {addr} -- ignoring')
            return
    except Exception as e:
        log(f'Exception while handling packet from {addr}: {e}')

def run_server(port, model_path, tokenizer_path, chunk_size, bind_ip='0.0.0.0'):
    log('Starting UDP file server...')
    log(f'Model path: {model_path}')
    log(f'Tokenizer path: {tokenizer_path}')
    log(f'Chunk size: {chunk_size}')

    files_map = {}
    try:
        files_map[FILE_MODEL] = ServedFile(model_path, FILE_MODEL, chunk_size)
        log(f'Loaded model: {model_path} ({files_map[FILE_MODEL].filesize} bytes, chunks={files_map[FILE_MODEL].chunk_count})')
    except Exception as e:
        log(f'ERROR loading model file: {e}')

    try:
        files_map[FILE_TOKENIZER] = ServedFile(tokenizer_path, FILE_TOKENIZER, chunk_size)
        log(f'Loaded tokenizer: {tokenizer_path} ({files_map[FILE_TOKENIZER].filesize} bytes, chunks={files_map[FILE_TOKENIZER].chunk_count})')
    except Exception as e:
        log(f'ERROR loading tokenizer file: {e}')

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # allow quick restarts
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((bind_ip, port))
    log(f'Listening on {bind_ip}:{port} for UDP requests...')

    try:
        while True:
            data, addr = sock.recvfrom(65535)
            handle_packet(sock, data, addr, files_map)
    except KeyboardInterrupt:
        log('Shutting down server (KeyboardInterrupt)...')
    finally:
        sock.close()

def main():
    parser = argparse.ArgumentParser(description='UDP file server for xv6 LLM weights (Milestone 3).')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help='UDP port to listen on (default 9999)')
    parser.add_argument('--model', type=str, default='stories15M.bin', help='Path to model weights file')
    parser.add_argument('--tokenizer', type=str, default='tokenizer.bin', help='Path to tokenizer file')
    parser.add_argument('--chunk-size', type=int, default=DEFAULT_CHUNK_SIZE, help='Chunk size in bytes (default 1024)')
    parser.add_argument('--bind', type=str, default='0.0.0.0', help='Bind IP (default 0.0.0.0)')
    args = parser.parse_args()

    if args.chunk_size <= 0 or args.chunk_size > 1400:
        print('Warning: chunk-size should be between 1 and ~1400 for safe UDP MTU behavior. Using default 1024.')
        args.chunk_size = DEFAULT_CHUNK_SIZE

    model_path = os.path.abspath(args.model)
    tokenizer_path = os.path.abspath(args.tokenizer)
    run_server(args.port, model_path, tokenizer_path, args.chunk_size, bind_ip=args.bind)

if __name__ == '__main__':
    main()
