import os

# 1. Folders and files to completely ignore
IGNORE_DIRS = {
    'build', '.git', '.spring', '.config', 'cont', 'share', 'fontcache', 
    'demos', '__pycache__', 'node_modules', 'third_party', 'lib'
}
INCLUDE_EXTS = {'.cpp', '.h', '.hpp', '.c', '.cc', '.lua', '.cfg', '.txt'}

# 2. THE BATCH LIST: Add any subfolders here you want to generate a context file for.
# The script will create a separate .txt file for each one.
TARGET_FOLDERS = [
    'rts/Rendering/Gfx',
    'rts/Game/UI',
    'rts/aGui',
    'rts/Sim',
    'rts/Lua',
    'rts/Net'
]

def get_file_content(filepath):
    """Safely reads file content."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            return f.read()
    except Exception:
        return "<Binary or Unreadable Data>"

def generate_context_for_target(root_dir, target_folder):
    """Generates a dedicated context file for a specific subsystem."""
    # Create a safe file name (e.g., "rts/Rendering/Gfx" -> "rts_Rendering_Gfx")
    sanitized_name = target_folder.replace('/', '_').replace('\\', '_')
    output_file = f"architecture_context_{sanitized_name}.txt"
    
    print(f"Generating {output_file}...")

    with open(output_file, 'w', encoding='utf-8') as out:
        out.write(f"# Recoil Engine Architecture Context: {target_folder}\n")
        out.write("This file contains the macro directory tree and the specific source code for this subsystem.\n\n")
        
        # Part 1: Generate the full directory tree (so Copilot knows where it fits)
        out.write("## 1. Complete Directory Tree\n```text\n")
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
            level = root.replace(root_dir, '').count(os.sep)
            indent = ' ' * 4 * (level)
            out.write(f"{indent}{os.path.basename(root)}/\n")
            
            subindent = ' ' * 4 * (level + 1)
            for f in files:
                if any(f.endswith(ext) for ext in INCLUDE_EXTS):
                    out.write(f"{subindent}{f}\n")
        out.write("```\n\n")

        # Part 2: Generate File Contents (ONLY for the target folder)
        out.write(f"## 2. Source Files for {target_folder}\n")
        file_count = 0
        
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
            for f in files:
                if any(f.endswith(ext) for ext in INCLUDE_EXTS):
                    filepath = os.path.join(root, f)
                    rel_path = os.path.relpath(filepath, root_dir)
                    
                    # Normalize paths for matching
                    normalized_rel_path = rel_path.replace('\\', '/')
                    
                    # If the file is inside our target folder, include its code!
                    if normalized_rel_path.startswith(target_folder):
                        out.write(f"\n### File: {normalized_rel_path}\n")
                        out.write("```cpp\n")
                        out.write(get_file_content(filepath))
                        out.write("\n```\n")
                        file_count += 1
                        
    print(f"  -> Successfully added {file_count} files.")

if __name__ == "__main__":
    PROJECT_ROOT = "."  
    print("Starting batch context generation...\n")
    
    for target in TARGET_FOLDERS:
        # Check if the folder actually exists before trying to map it
        if os.path.isdir(os.path.join(PROJECT_ROOT, target)):
            generate_context_for_target(PROJECT_ROOT, target)
        else:
            print(f"Skipping '{target}' (Directory not found)")
            
    print("\nAll subsystem context files generated successfully!")