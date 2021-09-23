entry = "0,hm,0,Write,"
start = 0
with open("4GB.txt", "w", encoding = "utf-8") as f:
    for i in range(1000000):
        f.write(entry + str(start) + ",4096,0\n")
        start += 4096
