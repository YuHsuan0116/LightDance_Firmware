import struct
frame_num = 0

def calculate_checksum(data):
    return sum(data) & 0xFFFFFFFF

def read_frame(frame_data, frame_index, of_channel, strip_channel):
    print(f"Frame{frame_index}:")
    offset = 0

    start_time = struct.unpack_from('<I', frame_data, offset)[0]
    offset += 4
    print(f"  start_time: {start_time}")
    
    fade = frame_data[offset]
    offset += 1
    print(f"  fade: {'on' if fade else 'off'}")
    
    for i, enabled in enumerate(of_channel):
        if enabled:
            g = frame_data[offset]
            r = frame_data[offset + 1]
            b = frame_data[offset + 2]
            offset += 3
            print(f"  OF[{i}]: G={g:03d}, R={r:03d}, B={b:03d}")
    
    for i, strip_leds in enumerate(strip_channel):
        if strip_leds > 0:
            for j in range(strip_leds):
                g = frame_data[offset]
                r = frame_data[offset + 1]
                b = frame_data[offset + 2]
                offset += 3
                print(f"  LED[{i}][{j}]: G={g:03d}, R={r:03d}, B={b:03d}")
    
    stored_checksum = struct.unpack_from('<I', frame_data, offset)[0]
    calculated_checksum = calculate_checksum(frame_data[:offset])
    
    print(f"  Checksum: {stored_checksum:08X} ({'OK' if stored_checksum == calculated_checksum else 'ERROR'})")
    return offset + 4

def read_control_file(filename):
    with open(filename, "rb") as file:
        print("=== control.dat ===")
        
        version = file.read(2)
        print(f"version: {version[0]}.{version[1]}")
        
        of_channel = []
        print("OF channel (40):")
        for i in range(40):
            enabled = struct.unpack('B', file.read(1))[0]
            of_channel.append(enabled)
            if i % 8 == 0 and i > 0:
                print()
            print(f"{enabled}", end=" ")
        print()
        
        print(f"enabled OF: {sum(of_channel)}")
        
        strip_channel = []
        print("\nStrip channel LED count (8):")
        for i in range(8):
            led_count = struct.unpack('B', file.read(1))[0]
            strip_channel.append(led_count)
            print(f"{led_count:3d}", end=" ")
        print()
        
        print(f"enabled Strip: {sum(1 for x in strip_channel if x > 0)}")
        print(f"total LED: {sum(strip_channel)}")
        
        frame_num = struct.unpack('<I', file.read(4))[0]
        print(f"\nframe_num: {frame_num}")
        
        timestamps = []
        for i in range(frame_num):
            timestamp_bytes = file.read(4)
            if len(timestamp_bytes) == 4:  # 檢查是否讀到4個字節
                timestamp = struct.unpack('<I', timestamp_bytes)[0]
                timestamps.append(timestamp)
                print(f"  {timestamp}")
            else:
                print(f"  ERROR: incomplete timestamp at frame {i}")
                break
        
        return version, of_channel, strip_channel, frame_num

def read_frame_file(filename, of_channel, strip_channel, control_version):
    with open(filename, "rb") as file:
        print("\n=== frame.dat ===")
        version = file.read(2)
        print(f"version: {version[0]}.{version[1]}")
        
        of_num = sum(of_channel)
        total_leds = sum(strip_channel)
        frame_size_without_checksum = 4 + 1 + (of_num * 3) + (total_leds * 3)
        frame_size = frame_size_without_checksum + 4
        
        print(f"frame size: {frame_size} bytes")
        
        frame_count = 0
        while frame_count < frame_num:
            frame_data = file.read(frame_size)
            if len(frame_data) != frame_size:
                break
            read_frame(frame_data, frame_count, of_channel, strip_channel)
            frame_count += 1

print("Reading Pattern Table files...")

version, of_channel, strip_channel, frame_num = read_control_file("control.dat")
read_frame_file("frame.dat", of_channel, strip_channel, version)