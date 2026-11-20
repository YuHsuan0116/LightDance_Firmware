import statistics
import time

import requests

ips = ["172.20.10.3"]

response = requests.get(f"http://{ips[0]}/api/status")
curruent_tsf_us = response.json()["tsf_us"]


for ip in ips:
    response = requests.post(
        f"http://{ip}/api/play",
        json={"play_at": curruent_tsf_us + 5 * 1000 * 1000},
    )
    print(response.json()["ok"])
