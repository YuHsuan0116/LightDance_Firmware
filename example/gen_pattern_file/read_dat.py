#!/usr/bin/env python3
import struct

def calculate_checksum(data):
    #計算checksum = 所有byte的和mod 2^32
    return sum(data) & 0xFFFFFFFF

def read_frame(frame_data, frame_index, of_num, strip_num, led_num_array, frame_size_without_checksum):
    #read單個 frame 的數據
    print(f"Frame{frame_index}: ")
    offset = 0

    #讀start_time
    start_time = struct.unpack_from('<I', frame_data, offset)[0]
    offset += 4
    print(f"  start_time: {start_time}")
    
    #讀fade
    fade = frame_data[offset]
    offset += 1
    print(f"  fade: {'True' if fade else 'False'}")
    
    #讀OF的GRB
    print("  OF colors: ")
    for i in range(of_num):
        g = frame_data[offset]
        r = frame_data[offset + 1]
        b = frame_data[offset + 2]
        offset += 3
        print(f"      OF[{i}]: G={g:03d}, R={r:03d}, B={b:03d}")
    
    #讀LED的GRB
    print("  LED colors: ")
    for i in range(strip_num):
        print(f"    strip{i}:")
        for j in range(led_num_array[i]):
            g = frame_data[offset]
            r = frame_data[offset + 1]
            b = frame_data[offset + 2]
            offset += 3
            print(f"      LED[{i}][{j}]: G={g:03d}, R={r:03d}, B={b:03d}")
    
    #讀並驗證checksum
    stored_checksum = struct.unpack_from('<I', frame_data, offset)[0]
    calculated_checksum = calculate_checksum(frame_data[:frame_size_without_checksum])
    
    print(f"  Checksum: stored={stored_checksum:08X}, calculated={calculated_checksum:08X} ", end="")
    if stored_checksum == calculated_checksum:
        print("✓ OK\n")
    else:
        print("✗ ERROR\n")

def read_control_file(filename):
    #讀control.dat
    try:
        with open(filename, "rb") as file:
            print("=== control.dat ===")
            
            #讀version跟 of_num, strip_num, led_num[]
            version_data = file.read(2)
            version = (version_data[0], version_data[1])
            print(f"version: {version[0]}.{version[1]}")
            
            of_num = struct.unpack('B', file.read(1))[0]
            strip_num = struct.unpack('B', file.read(1))[0]
            print(f"OF_num: {of_num}")
            print(f"LStrip_num: {strip_num}")
            
            led_num_array = []
            print("LED_num[]: ", end="")
            for _ in range(strip_num):
                led_num = struct.unpack('B', file.read(1))[0]
                led_num_array.append(led_num)
                print(f"{led_num}", end=" ")
            print()
            
            #讀frame_num, time_stamp[]
            frame_num = struct.unpack('<I', file.read(4))[0]
            print(f"Frame_num: {frame_num}")
            
            print("\ntime_stamp[]:")
            for i in range(frame_num):
                timestamp = struct.unpack('<I', file.read(4))[0]
                print(f"{timestamp}", end=" ")
            print()

            return version, of_num, strip_num, led_num_array, frame_num
            
    except FileNotFoundError:
        print("can't open control.dat")
        return None

def read_frame_file(filename, of_num, strip_num, led_num_array, control_version):
    #讀frame.dat
    try:
        print("\n=== frame.dat ===")
        with open(filename, "rb") as file:
            #讀版本並驗證
            version_data = file.read(2)
            version = (version_data[0], version_data[1])
            
            if version != control_version:
                print(f"ERROR: Version mismatch!")
                print(f"  control.dat version:{control_version[0]}.{control_version[1]}")
                print(f"  frame.dat version:{version[0]}.{version[1]}")
                return
            
            print(f"version:{version[0]}.{version[1]}")
            
            #算frame_size
            total_leds = sum(led_num_array)
            frame_size_without_checksum = 4 + 1 + (of_num * 3) + (total_leds * 3)
            frame_size_with_checksum = frame_size_without_checksum + 4
            print(f"frame size with checksum:{frame_size_with_checksum} bytes\n")
            
            frame_count = 0
            while True:
                #依序讀每個frame
                current_pos = file.tell()
                file.seek(0, 2)
                file_size = file.tell()
                file.seek(current_pos)
                
                if current_pos >= file_size:#大小有錯
                    break
                
                frame_data = file.read(frame_size_with_checksum)
                if len(frame_data) != frame_size_with_checksum:
                    break
                
                #讀每個frame
                read_frame(frame_data, frame_count, of_num, strip_num, led_num_array, frame_size_without_checksum)
                frame_count += 1
            
            file.seek(0, 2)
            file_size = file.tell()
            header_size = 2
            total_frames = (file_size - header_size) // frame_size_with_checksum
            
            print(f"file size:{file_size} bytes")
            print(f"total frames:{total_frames}")
            
    except FileNotFoundError:
        print("can't open frame.dat")

def main():
    print("=== Start reading Pattern Table file ===\n")
    
    result = read_control_file("control.dat")
    if result is None:
        return 1
    
    version, of_num, strip_num, led_num_array, frame_num = result
    read_frame_file("frame.dat", of_num, strip_num, led_num_array, version)
    
    return 0

if __name__ == "__main__":
    main()