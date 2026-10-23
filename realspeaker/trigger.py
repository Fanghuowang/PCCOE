import os
import time
import subprocess

AUDIO_FILE = "pccoe.mp3" 

def play_audio():
    print("Playing audio...")
    
    os.startfile(AUDIO_FILE) 
    
    # Wait 6 seconds for the song to play 
    time.sleep(6) 
    
    print("Closing media player...")
    # ONLY close Windows Media Player to avoid the error messages
    subprocess.call("taskkill /IM wmplayer.exe /F", shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # (The stdout and stderr parts just hide the text output)

print("Starting loop...")

while True:
    play_audio()
    print("Waiting 5 seconds...")
    time.sleep(5) 