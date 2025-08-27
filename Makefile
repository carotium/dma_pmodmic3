.PHONY: all

all: dma

dma: dmatest.c
	gcc dmatest.c -o dma

run: dma
	sudo ./dma
