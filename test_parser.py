import subprocess
import json
import sys
import os
import signal

def read_chat(channel):
    print(f"[*] Starting irc-parser for channel: {channel}")
    # Adjust path to the binary if needed based on where script is run
    binary_path = "./build/irc-parser"
    
    if not os.path.exists(binary_path):
        print(f"[!] Error: Could not find binary at {binary_path}")
        sys.exit(1)

    try:
        # Start the subprocess
        process = subprocess.Popen(
            [binary_path, channel], 
            stdout=subprocess.PIPE, 
            stderr=subprocess.PIPE, # Capture stderr so it doesn't pollute our terminal unless we want it
            text=True, # Automatically decode bytes to str
            bufsize=1  # Line buffered
        )

        def signal_handler(sig, frame):
            print("\n[*] Stopping...")
            process.terminate()
            sys.exit(0)
            
        signal.signal(signal.SIGINT, signal_handler)
        
        print("[*] Listening for messages (Press Ctrl+C to stop)...")
        print("-" * 50)
        
        message_count = 0
        
        for line in iter(process.stdout.readline, ''):
            if not line:
                break
                
            line = line.strip()
            if not line:
                continue
                
            try:
                message = json.loads(line)
                # Print beautifully formatted output
                print(f"[{message['channel']}] \033[94m{message['username']}\033[0m: {message['message']}")
                message_count += 1
                
                # Stop after 10 messages for testing
                if message_count >= 10:
                    print("-" * 50)
                    print(f"[*] Successfully parsed {message_count} messages! Test complete.")
                    process.terminate()
                    break
                    
            except json.JSONDecodeError:
                print(f"[!] Invalid JSON received: {line}")
                
        # Check if the process exited with an error
        return_code = process.wait()
        if return_code != 0 and return_code != -15: # -15 is SIGTERM
            print(f"[!] Process exited with code {return_code}")
            stderr_output = process.stderr.read()
            if stderr_output:
                print(f"[!] Stderr: {stderr_output}")

    except Exception as e:
        print(f"[!] Error: {e}")

if __name__ == "__main__":
    channel_to_test = "xqc" # Usually very active
    if len(sys.argv) > 1:
        channel_to_test = sys.argv[1]
    read_chat(channel_to_test)
