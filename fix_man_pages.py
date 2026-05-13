import re

with open('t-deck-cli/man_pages.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Find and replace the sdformat entry
start = content.find('{ "sdformat", "sdf", {')
if start > 0:
    end = content.find('}},', start) + 3
    new_entry = '''{ "sdformat", "sdf", {
        "SYNTAX   sdf [init]",
        "",
        "ABOUT    Format SD card to FAT32.",
        "WARNING  Destroys all data. Press y.",
        "MODES    sdf init - Format + init",
        nullptr
    }},'''
    content = content[:start] + new_entry + content[end:]
    
    with open('t-deck-cli/man_pages.cpp', 'w', encoding='utf-8') as f:
        f.write(content)
    
    print('Updated man_pages.cpp')
else:
    print('sdformat entry not found')
