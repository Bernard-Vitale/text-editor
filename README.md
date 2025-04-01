# Text Editor (C)

A simple text editor for the terminal, written in C. This project was developed following [this tutorial](https://viewsourcecode.org/snaptoken/kilo/index.html) and is currently a work in progress. Future plans include adding syntax highlighting for multiple languages, as well as implementing `Ctrl-C` (copy) and `Ctrl-V` (paste) functionality.

## Features
- Basic text editing capabilities
- Syntax Highlighting for C and C++
- Terminal-based interface
- Lightweight and minimalistic
- `Ctrl-F` to find text within the document
- Highlighting of found words, with arrow key navigation between occurrences

## Planned Features
- Syntax highlighting for additional programming languages
- Line Numbers
- `Ctrl-C` and `Ctrl-V` support for copy-pasting
- Additional quality-of-life improvements

## Installation
### Prerequisites
Ensure you have `gcc` (or another C compiler) and `make` installed on your system.

### Build and Run
```sh
git clone https://github.com/yourusername/text-editor.git
cd text-editor
make
./text-editor
```

## Usage
- Open without a file: `./text-editor`
- Open a file: `./text-editor filename`
- Navigate using arrow keys
- Edit text as needed
- Find text using `Ctrl-F`, with `F` highlighting found words and arrow keys navigating between results
- Save changes with `Ctrl-S`
- Exit with `Ctrl-Q`
