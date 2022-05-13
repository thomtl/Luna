#!/bin/bash

mkdir -p tmp/boot/grub
cp build/linux/arch/x86/boot/bzImage tmp/vmlinuz
echo -e "set default=0\nset timeout=0\nmenuentry \"Linux\" {\n linux /vmlinuz nokaslr earlyprintk=serial,ttyS0,9600 console=ttyS0,9600n8\n}" >> tmp/boot/grub/grub.cfg

grub-mkrescue -o build/linux-guest.iso tmp

rm -rf tmp