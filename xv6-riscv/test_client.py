import socket, struct

HOST = '127.0.0.1'
PORT = 25999

METADATA_REQUEST = 1
CHUNK_REQUEST = 3

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)

# metadata request for model (file_id=1)
sock.sendto(struct.pack('!B B', METADATA_REQUEST, 1), (HOST, PORT))
data, _ = sock.recvfrom(65535)
print('Metadata reply len', len(data))
# unpack: B B B I H I 32s
_, file_id, status, filesize, chunk_size, chunk_count, sha = struct.unpack('!B B B I H I 32s', data)
print('file_id', file_id, 'status', status, 'filesize', filesize, 'chunk_size', chunk_size, 'chunks', chunk_count)

# request chunk 0
sock.sendto(struct.pack('!B B I', CHUNK_REQUEST, 1, 0), (HOST, PORT))
data, _ = sock.recvfrom(65535)
# unpack header: B B I H
msg_type, file_id = struct.unpack('!B B', data[:2])
chunk_index = struct.unpack('!I', data[2:6])[0]
data_len = struct.unpack('!H', data[6:8])[0]
payload = data[8:]
print('chunk_index', chunk_index, 'data_len', data_len, 'payload_len', len(payload))
sock.close()
