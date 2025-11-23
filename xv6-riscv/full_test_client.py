import socket
import struct
import hashlib

SERVER_IP = "127.0.0.1"
SERVER_PORT = 25999
CHUNK_SIZE = 1024

# Pre-computed SHA-256 of stories15M.bin (replace with your known correct value)
REFERENCE_SHA256 = "your_precomputed_sha256_here"

# Protocol message types
MSG_METADATA = 1
MSG_DATA = 2

def request_metadata(sock):
    # METADATA_REQUEST: [type(1)][file_id(1)]
    # type = 1 (METADATA_REQUEST), file_id = 1 (model)
    pkt = struct.pack("!B B", 1, 1)
    sock.sendto(pkt, (SERVER_IP, SERVER_PORT))
    data, _ = sock.recvfrom(65535)
    
    # METADATA_RESPONSE: '!B B B I H I 32s'
    msg_type, file_id, status, filesize, chunk_size, chunk_count, sha256 = struct.unpack("!B B B I H I 32s", data)
    
    print(f"Metadata: filesize={filesize}, chunk_size={chunk_size}, total_chunks={chunk_count}")
    return filesize, chunk_size, chunk_count


def request_chunk(sock, chunk_index):
    # CHUNK_REQUEST: [type(1)][file_id(1)][chunk_index(4)]
    pkt = struct.pack("!B B I", 3, 1, chunk_index)
    sock.sendto(pkt, (SERVER_IP, SERVER_PORT))
    data, _ = sock.recvfrom(65535)
    
    # CHUNK_RESPONSE: [type(1)][file_id(1)][chunk_index(4)][payload_len(2)][payload]
    recv_type, file_id, idx, payload_len = struct.unpack("!B B I H", data[:8])
    payload = data[8:8+payload_len]
    return idx, payload


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)

    filesize, chunk_size, total_chunks = request_metadata(sock)

    reconstructed = bytearray()
    for i in range(total_chunks):
        idx, payload = request_chunk(sock, i)
        assert idx == i, f"Chunk index mismatch: expected {i}, got {idx}"
        reconstructed.extend(payload)
        if i % 1000 == 0:
            print(f"Fetched chunk {i}/{total_chunks}")

    reconstructed = reconstructed[:filesize]

    sha256 = hashlib.sha256(reconstructed).hexdigest()
    print(f"SHA-256 of reconstructed file: {sha256}")

    if REFERENCE_SHA256:
        if sha256 == "cd590644d963867a2b6e5a1107f51fad663c41d79c149fbecbbb1f95fa81f49a":
            print(" File integrity verified! SHA-256 matches reference.")
        else:
            print(" SHA-256 mismatch! File may be corrupted or incomplete.")

if __name__ == "__main__":
    main()
