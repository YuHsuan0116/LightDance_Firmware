import struct
import sys

COLOR_MAP = {
    'g': (255, 0, 0),
    'r': (0, 255, 0),
    'b': (0, 0, 255),
    'rg': (255, 255, 0),
    'gb': (255, 0, 255),
    'rb': (0, 255, 255),
    'w': (255, 255, 255)
}

def parse_color_input(color_str):
    color_str = color_str.lower().strip()
    if color_str not in COLOR_MAP:
        raise ValueError(f"choose: {', '.join(COLOR_MAP.keys())}")
    return COLOR_MAP[color_str]

def calculate_checksum(frame_data):
    return sum(frame_data) & 0xFFFFFFFF

def get_user_input():
    OF_NUM = int(input("num of OF: "))
    STRIP_NUM = int(input("num of Strip: "))
    
    LED_num_array = []
    for i in range(STRIP_NUM):
        led_num = int(input(f"total LED of Strip {i+1}: "))
        LED_num_array.append(led_num)
    
    color_input = input("color (g/r/b/rg/gb/rb/w): ").strip()
    color = parse_color_input(color_input)
    
    fade = int(input("fade (0 or 1): ") or "1")
    time_interval = int(input("time interval of each frame: "))
    FRAME_NUM = int(input("total frame_num: "))
    
    VERSION_MAJOR = 1
    VERSION_MINOR = 5
    
    return {
        'OF_NUM': OF_NUM,
        'STRIP_NUM': STRIP_NUM,
        'LED_num_array': LED_num_array,
        'color': color,
        'fade': fade,
        'time_interval': time_interval,
        'FRAME_NUM': FRAME_NUM,
        'VERSION_MAJOR': VERSION_MAJOR,
        'VERSION_MINOR': VERSION_MINOR
    }

def main():
    params = get_user_input()
    
    OF_NUM = params['OF_NUM']
    STRIP_NUM = params['STRIP_NUM']
    LED_num_array = params['LED_num_array']
    color = params['color']
    fade = params['fade']
    time_interval = params['time_interval']
    FRAME_NUM = params['FRAME_NUM']
    VERSION_MAJOR = params['VERSION_MAJOR']
    VERSION_MINOR = params['VERSION_MINOR']
    
    total_leds = sum(LED_num_array)
    
    frame_size_without_checksum = 4 + 1 + (OF_NUM * 3) + (total_leds * 3)
    frame_size_with_checksum = frame_size_without_checksum + 4
    
    with open("control.dat", "wb") as control_file:
        control_file.write(struct.pack('<BB', VERSION_MAJOR, VERSION_MINOR))
        control_file.write(struct.pack('<B', OF_NUM))
        control_file.write(struct.pack('<B', STRIP_NUM))
        
        for led_num in LED_num_array:
            control_file.write(struct.pack('<B', led_num))

        control_file.write(struct.pack('<I', FRAME_NUM))
        
        for k in range(FRAME_NUM):
            timestamp = k * time_interval
            control_file.write(struct.pack('<I', timestamp))
    
    print("\ncontrol.dat generate done")
    
    with open("frame.dat", "wb") as frame_file:
        frame_file.write(struct.pack('<BB', VERSION_MAJOR, VERSION_MINOR))
    
        for k in range(FRAME_NUM):
            frame_data = bytearray()
            start_time = k * time_interval
            frame_data.extend(struct.pack('<I', start_time))
            frame_data.append(fade)

            if k % 2 == 1:
                for i in range(OF_NUM):
                    frame_data.append(color[0])  # G
                    frame_data.append(color[1])  # R
                    frame_data.append(color[2])  # B
                
                for i in range(STRIP_NUM):
                    for j in range(LED_num_array[i]):
                        frame_data.append(color[0])  # G
                        frame_data.append(color[1])  # R
                        frame_data.append(color[2])  # B
            else:

                for i in range(OF_NUM):
                    frame_data.append(0)  # G
                    frame_data.append(0)  # R
                    frame_data.append(0)  # B
                
                for i in range(STRIP_NUM):
                    for j in range(LED_num_array[i]):
                        frame_data.append(0)  # G
                        frame_data.append(0)  # R
                        frame_data.append(0)  # B
            
            checksum = calculate_checksum(frame_data)
            frame_file.write(frame_data)
            frame_file.write(struct.pack('<I', checksum))
    
    print("\nframe.dat  generate done")

    print("=" * 50)
    print("parameter:")
    print(f"OF_num: {OF_NUM}")
    print(f"Strip_num: {STRIP_NUM}")
    print(f"total LED on each Strip: {' '.join(map(str, LED_num_array))}")
    print(f"total LED: {total_leds}")
    print(f"color (G,R,B): {color}")
    print(f"time interval: {time_interval}")
    print(f"total frame_num: {FRAME_NUM}")
    print(f"version: {VERSION_MAJOR}.{VERSION_MINOR}")
    print(f"size of a frame with checksum): {frame_size_with_checksum} byte")
    print("=" * 50)

if __name__ == "__main__":
    try:
        main()
    except ValueError as e:
        print(f"input error: {e}")
        sys.exit(1)