#!/bin/sh
ctags -R .
gvim Makefile configure src/*
