import struct

# 讀取 frame.dat 並顯示每3個字元一組
with open("frame.dat", "rb") as file:
    count = 0
    print("-" * 30)
    
    while True:
        # 一次讀取3個bytes
        data = file.read(1)
        if not data:
            break
        
        # 顯示16進制和10進制
        hex_str = " ".join(f"{b:02X}" for b in data)
        dec_str = " ".join(f"{b:3d}" for b in data)
        
        print(f"[{count:4d}] {hex_str}  |  {dec_str}")
        count += 1

print(f"\n總共讀取 {count} ")