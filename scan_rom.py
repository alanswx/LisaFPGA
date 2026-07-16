import capstone, sys
hi=[int(l,16) for l in open('rtl/CPU_HIGH_H.mem') if l.strip()]
lo=[int(l,16) for l in open('rtl/CPU_LOW_H.mem') if l.strip()]
rom=bytearray()
for h,l in zip(hi,lo): rom.extend((h,l))
md=capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_M68K_000)
base=0xFE0000

mode = sys.argv[1] if len(sys.argv)>1 else 'scan'
if mode=='scan':
    # find move.b #$86,(a0)  and  move.b #$88,$2(a0)  and bset #17,d7 / bset #$11,d7
    for i in md.disasm(bytes(rom), base):
        s=f'{i.mnemonic} {i.op_str}'
        if i.mnemonic.startswith('move') and ('#$86' in s):
            print(f'{i.address:06X}: {s}   [arm/cmd $86]')
        if i.mnemonic=='bset' and ('d7' in s) and ('$11' in s or '#$11' in s):
            print(f'{i.address:06X}: {s}   [bset #17,d7]')
        if i.mnemonic=='bset' and ('#$11' in s):
            print(f'{i.address:06X}: {s}')
else:
    start=int(sys.argv[1],16); end=int(sys.argv[2],16)
    for i in md.disasm(bytes(rom[start-base:end-base]), start):
        print(f'{i.address:06X}: {i.mnemonic:8s} {i.op_str}')
