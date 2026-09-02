"""
==================================================
STREAMING AI ASSISTANT SERVER
FOR ESP32-S3 PUSH-TO-TALK CLIENT
==================================================

INSTALL
-------
pip install websockets
pip install faster-whisper
pip install requests
pip install numpy
pip install pydub
pip install audioop-lts

RUN
---
python stream_server.py

ESP32 CONNECTS TO
-----------------
ws://YOUR_PC_IP:9000

==================================================
"""
import asyncio
from pickle import TRUE
import websockets
import tempfile
import os
import json
import wave
import uuid
import requests
import time
import re
import base64
from datetime import datetime
from PIL import Image
from faster_whisper import WhisperModel
from pydub import AudioSegment
from io import BytesIO

# ==================================================
# CLIENT CONFIG CLASS
# ==================================================

class ClientConfig:

    def __init__(self,data):

        self.role = data.get(
            "ROLE",
            "assistent" #assistant/chatbot
        )

        self.has_history = data.get(
            "HAS_HISTORY",
            "no" #yes/no
        )

        self.input_refining = data.get(
            "INPUT_REFINING",
            "no" #yes/no
        )

        self.picture_input_refining = data.get(
            "PICTURE_INPUT_REFINING",
            "no" #yes/no
        )

        self.voice_output = data.get(
            "VOICE_OUTPUT",
            "yes" #yes/no/conditional
        )

        self.picture_output = data.get(
            "PICTURE_OUTPUT",
            "yes" #yes/no/conditional
        )

        self.voice_name = data.get(
            "VOICE_NAME",
            "af_heart"
        )

        self.start_image = data.get(
            "START_IMAGE",
            "AI"
        )

        self.confirm_request = data.get(
            "CONFIRM_REQUEST",
            "no"
        )

        self.auto_run = data.get(
            "AUTO_RUN",
            "no"
        )

        self.image_request_check = data.get(
            "IMAGE_REQUEST_CHECK",
            "no"
        )

        self.name = data.get(
            "NAME",
            "AI"
        )

        self.user_name = data.get(
            "USER_NAME",
            "User"
        )

        self.personality = data.get(
            "PERSONALITY",
            ""
        )

        self.lore = data.get(
            "LORE",
            ""
        )

        self.appearance_body = data.get(
            "APPEARANCE_BODY",
            ""
        )

        self.appearance_clothing = data.get(
            "APPEARANCE_CLOTHING",
            ""
        )

        self.positive_prompt = data.get(
            "POSITIVE_PROMPT",
            ""
        )

        self.negative_prompt = data.get(
            "NEGATIVE_PROMPT",
            ""
        )

        self.model_name = data.get(
            "MODEL_NAME",
            ""
        )

        self.steps = data.get(
            "STEPS",
            ""
        )

        self.cfg_scale = data.get(
            "CFG_SCALE",
            ""
        )

        self.sampler_name = data.get(
            "SAMPLER_NAME",
            ""
        )

        self.scheduler = data.get(
            "SCHEDULER",
            ""
        )

        self.text_to_image_config = data.get(
            "TEXT_TO_IMAGE_CONFIG",
            ""
        )

        self.input_refining_config = data.get(
            "INPUT_REFINING_CONFIG",
            ""
        )

        self.image_request_check_config = data.get(
            "IMAGE_REQUEST_CHECK_CONFIG",
            ""
        )

        self.Positive_Prompt_Extractor_Config = data.get(
            "POSITIVE_PROMPT_EXTRACTOR_CONFIG",
            ""
        )

# ==================================================
# SERVER
# ==================================================

HOST = "0.0.0.0"
PORT = 9000
DEBUG = True

# ==================================================
# KOBOLDCPP
# ==================================================

KOBOLD_URL = "http://127.0.0.1:5001/api/extra/generate/stream"

KOBOLD_HEALTH = "http://127.0.0.1:5001/api/v1/model"


# ==================================================
# KOKORO-WEB
# ==================================================


KOKOROWEB_URL = "http://localhost:3000/api/v1/audio/speech"

KOKOROWEB_HEALTH = "http://localhost:3000/health"

VOICE_NAME = "af_heart"

# ==================================================
# WHISPER
# ==================================================

WHISPER_MODEL = "medium"

# ==================================================
# STABLE-DIFFUSION-WEBUI
# ==================================================

STABLEDIFFUSION_URL = "http://127.0.0.1:7860/sdapi/v1/txt2img"

STABLEDIFFUSION_HEALT = "http://127.0.0.1:7860/sdapi/v1/sd-models"

SD_WIDTH = 800

SD_HEIGHT = 1100

#CLIENT_WIDTH =320
#CLIENT_HEIGHT =480
#0.4

# ==================================================
# LOAD ASSISTANT CONMFIG
# ==================================================

CONFIG_DIR = "configs"
PROMPT_DIR = "prompts"
IMAGE_DIR = "images"
RESOURCE_DIR = "resources"

def load_assistant_config(filename):

    path = os.path.join(CONFIG_DIR, filename)

    if not os.path.exists(path):
        raise Exception(
            f"Config file not found: {path}"
        )


    config = {}

    current_key = None
    buffer = []


    with open(path, "r", encoding="utf-8") as f:

        for line in f:

            line = line.rstrip()


            # Empty line
            if not line:
                if current_key:
                    buffer.append("")
                continue


            # New key
            if "=" in line and not line.startswith(" "):

                if current_key:
                    config[current_key] = "\n".join(buffer).strip()
                    log(current_key + ": " + config[current_key])


                current_key, value = line.split("=",1)
                current_key = current_key.strip()
                buffer = []

                if value:
                    buffer.append(value)

            else:

                buffer.append(line)


    if current_key:
        config[current_key] = "\n".join(buffer).strip()


    return config

# ==================================================
# LOAD TEXT FILE
# ==================================================

def read_text_file(DIR, filename):
    
    path = os.path.join(DIR, filename)

    try:
        # 'r' mode opens the file for reading (text mode by default)
        with open(path, 'r', encoding='utf-8') as file:
            content = file.read()
            return content
    except FileNotFoundError:
        return "Error: The file was not found." + path
    except Exception as e:
        return f"An error occurred: {e}"


# ==================================================
# MEMORY
# ==================================================

conversation_history = []

MAX_HISTORY = 80

# ==================================================
# LOGGING
# ==================================================

def log(message):

    now = datetime.now().strftime(
        "%H:%M:%S"
    )

    print(f"[{now}] {message}")

# ==================================================
# LOAD WHISPER
# ==================================================

log("Loading Whisper model...")
model = WhisperModel(WHISPER_MODEL,device="cpu",compute_type="int8")
log("Whisper loaded")

# ==================================================
# CLEAN AI RESPONSE
# ==================================================

def clean_ai_response(text):


    # Remove <think> blocks
    text = re.sub(r"<think>.*?</think>", "",text,flags=re.DOTALL )

    # Remove leftover tags
    text = re.sub(

        r"</?think>",

        "",

        text
    )

    # Remove extra whitespace
    text = re.sub(

        r"\n\s*\n",

        "\n",

        text
    )

    # Remove markdown
    text = text.replace("*", "")
    text = text.replace("#", "")

    # Remove roleplay
    text = re.sub(
        r"\(.*?\)",
        "",
        text
    )

    return text.strip()

# ==================================================
# VALIDATE AI RESPONSE
# ==================================================

def is_valid_response(text):

    if not text:
        return False

    # Empty
    if len(text.strip()) == 0:
        return False

    # Only think tags
    cleaned = clean_ai_response(text).strip()

    if len(cleaned) == 0:
        return False

    # Garbage tokens
    invalid = [

        "<think>",
        "</think>",
        "null",
        "None"
    ]

    if cleaned in invalid:
        return False

    return True

# ==================================================
# CONVERT AUDIO FOR ESP32
# ==================================================

def convert_audio_for_esp32(audio_bytes):

    log("Converting audio...")
    log("")
    # Load WAV
    audio = AudioSegment.from_file(BytesIO(audio_bytes),format="wav")

    # Convert format
    audio = audio.set_frame_rate(16000)
    audio = audio.set_channels(1)
    audio = audio.set_sample_width(2)

    # Export RAW PCM
    raw_audio = audio.raw_data

    log(
        f"Converted audio: "
        f"{len(raw_audio)} bytes"
    )
    log("")
    return raw_audio

# ==================================================
# HEALTH CHECKS
# ==================================================

def check_kobold():

    try:

        response = requests.get(

            KOBOLD_HEALTH,
            timeout=5
        )

        if response.status_code == 200:

            data = response.json()

            log(
                "[OK] KoboldCpp ready"
            )
            log("")
            log(
                f"Model: "
                f"{data.get('result')}"
            )
            log("")
            return True

        else:

            log(
                f"[ERROR] KoboldCpp status: "
                f"{response.status_code}"
            )

    except Exception as e:

        log(
            "[ERROR] KoboldCpp offline"
        )

        log(str(e))

    return False

def check_KokoroWeb():

    try:

        payload = {
            "model": "model",
            "voice": "af_heart",
            "input": "ready",
            "response_format": "wav"
        }

        response = requests.post(
            KOKOROWEB_URL,
            json=payload,
            timeout=20
        )


        if response.status_code == 200:

            if len(response.content) > 1000:

                log(
                    "[OK] KokoroWeb TTS ready"
                )
                log("")
                return True


        log(
            f"[ERROR] KokoroWeb returned "
            f"{response.status_code}"
        )

        log(response.text)


    except requests.exceptions.ConnectionError:

        log(
            "[ERROR] KokoroWeb offline"
        )


    except Exception as e:

        log(
            "[ERROR] KokoroWeb check failed"
        )

        log(str(e))


    return False

def check_stableDiffusion():

    try:

        response = requests.get(

            STABLEDIFFUSION_HEALT,
            timeout=5
        )

        if response.status_code == 200:
            log("Stable Diffusion WebUI")
            return True

    except Exception as e:

        log("[ERROR] Stable Diffusion WebUI offline")

        log(str(e))

    return False


def wait_for_services():

    log("Checking services...")
    log("")
    while True:

        kobold_ok = check_kobold()
        kokoro_ok = check_KokoroWeb()
        diffusion_ok = check_stableDiffusion()

        if kobold_ok and kokoro_ok and diffusion_ok:

            log("All services ready")
            log("")
            return True

        log("Retrying in 5 seconds...")
        log("")
        time.sleep(5)

# ==================================================
# MEMORY
# ==================================================

def save_history(role, content):
    global conversation_history
    conversation_history.append({"role": role, "content": content})

    if len(conversation_history) > MAX_HISTORY:
        conversation_history = conversation_history[-MAX_HISTORY:]

# ==================================================
# PROMPT
# ==================================================

def build_prompt(user_text, config):

    if config.has_history == "yes":
        history_lines = []

        for msg in conversation_history[-MAX_HISTORY:]:
            role = "user" if msg['role'] == config.user_name else "assistant"
            history_lines.append(f"<|im_start|>{role}\n{msg['content']}<|im_end|>\n")

        history = "".join(history_lines)
    else:
        history = ""

    prompt = (
    f"<|im_start|>system\n"
    f"{config.personality}\n\n"
    f"{config.lore}"
    f"<|im_end|>\n"
    f"{history}"
    f"<|im_start|>user\n{user_text}<|im_end|>\n"
    f"<|im_start|>assistant\n"
)

    return prompt

def build_prompt_simple(imput_text):



    prompt = (
    f"<|im_start|>system\n"
    f"{imput_text}\n\n"
    f"<|im_end|>\n"
    f"<|im_start|>assistant\n"
)

    return prompt

def build_prompt_simple_and_history(imput_text, config):

    history_lines = []

    for msg in conversation_history[-MAX_HISTORY:]:
        role = "user" if msg['role'] == config.user_name else "assistant"
        history_lines.append(f"<|im_start|>{role}\n{msg['content']}<|im_end|>\n")

    history = "".join(history_lines)

    prompt = (
    f"<|im_start|>system\n"
    f"{imput_text}\n\n"
    f"<|im_end|>\n"
    f"{history}"
    f"<|im_start|>assistant\n"
)

    return prompt

# ==================================================
# SAVE WAV
# ==================================================

def save_wav(raw_audio,output_path):

    with wave.open(output_path,"wb") as wf:

        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(16000)
        wf.writeframes(raw_audio)

# ==================================================
# TRANSCRIBE
# ==================================================

def transcribe_audio(wav_path):

    log("Transcribing...")
    log("")
    start = time.time()

    segments, info = model.transcribe(

        wav_path,

        beam_size=5,

        vad_filter=True
    )

    text = ""

    for seg in segments:

        text += seg.text

    elapsed = round(
        time.time() - start,
        2
    )

    log(
        f"Transcription took "
        f"{elapsed}s"
    )
    log("")
    return text.strip()

# ==================================================
# STREAM KOBOLDCPP
# ==================================================

async def stream_kobold(prompt):

    payload = {

        "prompt": prompt,
        "max_context_length": 4096,
        "max_length": 1024,
        "temperature": 0.8,
        "top_p": 0.9,
        "top_k": 30,
        "top_a": 0,
        "rep_pen": 1.05,
        "stop_sequence": ["\nUser:", "<|im_end|>", "\nAI:"],
        "stream": True,
        "trim_stop": True,
        "reasoning_effort": "none"
    }

    # log(
    #     "Sending to KoboldCpp..."
    # )
    # log("")

    response = requests.post(

        KOBOLD_URL,

        json=payload,

        stream=True
    )

    # log(
    #     f"Kobold status: "
    #     f"{response.status_code}"
    # )
    # log("")

    # STREAM TOKENS
    for line in response.iter_lines():

        if not line:

            continue

        decoded = line.decode(
            "utf-8"
        )

        # SSE format
        if not decoded.startswith(
            "data:"
        ):

            continue

        # Remove "data:"
        chunk = decoded[5:].strip()

        # End marker
        if chunk == "[DONE]":

            break

        # Parse JSON
        try:

            data = json.loads(
                chunk
            )

        except Exception as e:

            log(
                f"JSON parse error: {e}"
            )

            continue

        # Extract token
        token = data.get("token","")

        # Skip empty
        if not token:

            continue

        # Debug print
        if DEBUG:

            print(
                "",#token
                end="",
                flush=True
            )

        # Yield ONLY TOKEN
        yield token

    print()

# ==================================================
# GENERATE SD IMAGE
# ==================================================

def generate_image(prompt, config, width, height):

    filename = f"{datetime.now():%Y-%m-%d_%H-%M-%S}"
    filename = filename.replace("-", "").replace(":", "").replace(" ", "_")

    payload = {
        "prompt": config.positive_prompt  + " " + prompt,
        "negative_prompt":config.negative_prompt,
        "sd_model_checkpoint": config.model_name,
        "seed": -1,
        "steps": config.steps,
        "width": width,
        "height": height,
        "cfg_scale": config.cfg_scale,
        "sampler_name": config.sampler_name,
        "scheduler": config.scheduler
    }

    response = requests.post(
        STABLEDIFFUSION_URL,
        json=payload,
        timeout=300
    )

    response.raise_for_status()

    result = response.json()

    # SD WebUI returns the image as Base64
    image_base64 = result["images"][0]

    # Decode and save
    image_data = base64.b64decode(image_base64)

    output_path = os.path.join(IMAGE_DIR, filename + ".png")

    # Save PNG
    with open(output_path, "wb") as f:
        f.write(image_data)

    return filename

def downscale_image(filename, scale_factor=0.4):

    input_path = os.path.join(IMAGE_DIR, filename + ".png")

    if not os.path.exists(input_path):
        print(f"Error: The file {input_path} does not exist.")
        return

    output_path = os.path.join(IMAGE_DIR, filename + "_S.png")

    # Open the image
    with Image.open(input_path) as img:
        # Calculate new dimensions
        new_width = int(img.width * scale_factor)
        new_height = int(img.height * scale_factor)
        
        scaled_img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
        
        # Save the scaled image
        scaled_img.save(output_path, format="PNG")
        print(f"Successfully scaled from {img.width}x{img.height} to {new_width}x{new_height}")
        print(f"Saved to: {output_path}")

        return filename + "_S"

# ==================================================
# KokoroWeb TTS
# ==================================================

def generate_tts(text, config):

    log("Generating TTS...")
    log("")
    payload = {
        "model": "model",
        "voice": config.voice_name,
        "input": text,
        "response_format": "wav"
    }
    log("------------------------------------------------------------------------------------------------------")
    log("TTS Payload:")
    print(json.dumps(payload, indent=2))
    log("------------------------------------------------------------------------------------------------------")

    try:
        response = requests.post(
            KOKOROWEB_URL,
            json=payload,
            timeout=120
        )

    except Exception as e:
        raise Exception(
            f"Kokoro Web connection failed: {e}"
        )


    log(
        f"Kokoro Web status: "
        f"{response.status_code}"
    )
    log("")

    # HTTP error handling
    if response.status_code != 200:
        try:
            print(
                json.dumps(
                    response.json(),
                    indent=2
                )
            )
        except:
            print(response.text)

        raise Exception(
            f"Kokoro Web TTS failed: "
            f"{response.status_code}"
        )



    content_type = response.headers.get("content-type", "")


    log(
        f"Content-Type: {content_type}"
    )
    log("")

    # Direct audio response
    if ("audio" in content_type
       or 
        "wav" in content_type
    ):

        log(
            f"Received audio: "
            f"{len(response.content)} bytes"
        )
        log("")
        return response.content


    # JSON response fallback
    try:

        data = response.json()

        print(
            json.dumps(
                data,
                indent=2
            )
        )

    except Exception:

        raise Exception(
            "Kokoro returned unknown response:\n"
            + response.text
        )


    audio_url = (
        data.get("audio_url")
        or
        data.get("url")
        or
        data.get("output_file_url")
    )


    if not audio_url:

        raise Exception(
            "No audio returned from Kokoro Web.\n"
            f"Response: {data}"
        )


    # Download returned file
    if audio_url.startswith("/"):

        audio_url = ("http://127.0.0.1:3000" + audio_url)


    log("Downloading audio:")
    log("")
    log(audio_url)
    log("")


    audio_response = requests.get(
        audio_url,
        timeout=120
    )


    log(
        f"Downloaded "
        f"{len(audio_response.content)} bytes"
    )
    log("")


    return audio_response.content

# ==================================================
# STREAM AUDIO
# ==================================================

async def stream_audio(websocket, audio_bytes):

    SAMPLE_RATE = 16000
    BYTES_PER_SAMPLE = 2
    CHANNELS = 1

    BYTES_PER_SECOND = (
        SAMPLE_RATE * BYTES_PER_SAMPLE * CHANNELS
    )

    # 20 ms of PCM audio
    CHUNK = 640

    log(f"Streaming {len(audio_bytes)} bytes")
    log(f"Bytes/sec: {BYTES_PER_SECOND}")
    log(f"Chunk: {CHUNK} bytes / 20 ms")
    log("")

    # Use a monotonic clock so timing does not accumulate jitter.
    loop = asyncio.get_running_loop()

    start_time = loop.time()
    bytes_sent = 0

    for i in range(0, len(audio_bytes), CHUNK):
        chunk = audio_bytes[i:i + CHUNK]

        await websocket.send(chunk)

        bytes_sent += len(chunk)

        # When this audio should have been sent.
        target_time = start_time + (
            bytes_sent / BYTES_PER_SECOND
        )

        # Sleep only until the correct real-time position.
        delay = target_time - loop.time()

        if delay > 0:
            await asyncio.sleep(delay)

    await websocket.send("AUDIO_END")

    log("Audio complete")
    log("")


# ==================================================
# SENDING IMAGE
# ==================================================

async def send_image(websocket, DIR, filename: str):

    image_path = os.path.join(DIR, filename + ".png")

    if not os.path.exists(image_path):
        raise FileNotFoundError(f"Image not found at: {image_path}")
        
    # Read the file in read-binary ('rb') mode
    with open(image_path, 'rb') as image_file:
        binary_data = image_file.read()
        
    print(f"First bytes: {binary_data[:16].hex(' ')}")

    # Send the raw bytes over the active connection
    print(f"Sending {image_path} ({len(binary_data)} bytes)...")
    await websocket.send(binary_data)

    await websocket.send("IMAGE_END")
    print("Image sent successfully.")

# ==================================================
# GET CONFIG LIST
# ==================================================

def get_config_list():
    return ", ".join([f for f in os.listdir(CONFIG_DIR) if os.path.isfile(os.path.join(CONFIG_DIR, f))])

# ==================================================
# CLIENT HANDLER
# ==================================================

async def handle_client(websocket):

    log("ESP32 connected")
    log("")

    await websocket.send("ConfigList:" + get_config_list())
    log("Config list send")
    log("")

    log("Wait for client configuration")
    log("")
    # Wait for client configuration
    config_file = await websocket.recv()

    if not isinstance(config_file,str):
        await websocket.close()
        return

    log(f"Loading assistant config: {config_file}")

    try:
        data = load_assistant_config(config_file)
        client_config = ClientConfig(data)


    except Exception as e:
        log(str(e))

        await websocket.send("CONFIG_ERROR")
        return

    log("Config loaded successfully.")
    log("")

    conversation_history.clear()
    log("Conversation history cleared.")
    log("")

    if client_config.confirm_request == "yes":
        await websocket.send("CONFIRM_REQUEST")

    if client_config.start_image != "":
        log("Sending defalt image.")
        log("")
        await websocket.send("IMAGE_START")
        await send_image(websocket,RESOURCE_DIR,client_config.start_image)

    await websocket.send("Ready")
    log("Ready, waiting for speech.")
    log("")


    audio_buffer = bytearray()

    try:
        async for message in websocket:
        
            # AUDIO
            if isinstance(message,bytes):
                audio_buffer.extend(message)

            # END OF SPEECH
            elif message == "AUDIO_END":

                log("Speech ended")
                log("")

                # SAVE TEMP WAV          
                temp_path = os.path.join(tempfile.gettempdir(),str(uuid.uuid4())+ ".wav")

                save_wav(audio_buffer,temp_path)

                # WHISPER
                user_text = transcribe_audio(temp_path)

                audio_buffer = bytearray()
                log("Cleard audio")
                log("")
                try:
                  os.remove(temp_path)

                except:
                  pass

                if len(user_text.strip()) == 0:

                    log("Empty speech")
                    log("")

                    # Tell ESP32 to resume listening
                    await websocket.send("NO_SPEECH")

                    audio_buffer = bytearray()

                    continue
                else:
                    if client_config.auto_run == "yes":
                        await websocket.send("AUTO_RUN")

                    await websocket.send("userText:" + user_text)
                    log("------------------------------------------------------------------------------------------------------")
                    log(
                        f"{client_config.user_name}: "
                        f"{user_text}"
                    )
                    log("------------------------------------------------------------------------------------------------------")

            elif message == "SUBMIT":
                log("Imput submited")
                log("")


                if client_config.role == "chatbot":
                    log("Runing as chatbot")
                    log("")
                elif client_config.role == "assistant":
                    log("Runing as assistant")
                    log("")

                    if client_config.image_request_check == "yes":
                        log("Checking if user requests an image")
                        log("")

                        image_request_check_prompt = build_prompt_simple(read_text_file(PROMPT_DIR, client_config.image_request_check_config) + " " + user_text)

                        image_request_check_ai_response = ""

                        async for token in stream_kobold(image_request_check_prompt):
                            image_request_check_ai_response += token

                        if "yes" in image_request_check_ai_response:
                            request_image = True
                            log("User requested an image.")
                            log("")
                        else:
                            request_image = False
                    else:
                            request_image = False

                    if client_config.input_refining == "yes" and request_image == False:
                        log("Runing input refining on the user text")
                        log("")

                        if request_image:
                            refining_prompt = build_prompt_simple("The LLM output is going to be a SD image. " + read_text_file(PROMPT_DIR, client_config.input_refining_config) + " " + user_text)
                        else:
                            refining_prompt = build_prompt_simple(read_text_file(PROMPT_DIR, client_config.input_refining_config) + " " + user_text)

                        refining_ai_response = ""

                        async for token in stream_kobold(refining_prompt):
                            refining_ai_response += token

                        user_text = clean_ai_response( refining_ai_response.strip())

                    # PROMPT
                    prompt = build_prompt(user_text,client_config)

                    log("------------------------------------------------------------------------------------------------------")
                    log("The Prompt: \n" + user_text)
                    log("------------------------------------------------------------------------------------------------------")    

                    # KOBOLD
                    ai_response = ""

                    async for token in stream_kobold(prompt):
                        ai_response += token

                    ai_response = clean_ai_response( ai_response.strip())
             
                    # Validate response              
                    if not is_valid_response(ai_response):
                        ai_response = "Can you repeat that?"
                    else:
                        # SAVE MEMORY IF VALID
                        save_history(client_config.user_name,user_text)            
                        save_history(client_config.name,ai_response)
                    log("------------------------------------------------------------------------------------------------------")
                    log(
                        f"AI Response: "
                        f"{ai_response}"
                    )
                    log("------------------------------------------------------------------------------------------------------")
                    
                    if client_config.voice_output == "yes" and request_image == False:
                        log("Generateing audio")
                        log("")
                        # TTS             
                        tts_audio = generate_tts(ai_response,client_config)
              
                        # convert audio for esp32
                        tts_audio = convert_audio_for_esp32(tts_audio)

                        # STREAM AUDIO
                        await websocket.send("SendingAudio")
                        await websocket.send("AUDIO_START")
                        await stream_audio(websocket,tts_audio)
                    elif client_config.voice_output == "yes" and request_image == True:
                        log("Generateing audio")
                        log("")
                        # TTS             
                        tts_audio = generate_tts("Sure! I am creating an image for you now.",client_config)

                        # convert audio for esp32
                        tts_audio = convert_audio_for_esp32(tts_audio)

                        # STREAM AUDIO
                        await websocket.send("SendingAudio")
                        await websocket.send("AUDIO_START")
                        await stream_audio(websocket,tts_audio)

                    if client_config.picture_output == "yes" or request_image:
                        log("Preparing prompt for SD")
                        log("")

                        sd_prompt= ""

                        if client_config.picture_input_refining == "yes":
                            log("Runing input refining on the AI response")
                            log("")
                            sd_prompt_refining = build_prompt_simple(read_text_file(PROMPT_DIR, client_config.text_to_image_config) + " " + ai_response)
                            
                            ai_response = ""

                            async for token in stream_kobold(sd_prompt_refining):
                                ai_response += token


                            ai_response = clean_ai_response( ai_response.strip())
 
                        log("Geting the positive prompt")
                        log("")
                        positive_ai_response = ""
                        positive_prompt = build_prompt_simple(read_text_file(PROMPT_DIR, client_config.Positive_Prompt_Extractor_Config) + " " + ai_response)

                        async for token in stream_kobold(positive_prompt):
                            positive_ai_response += token

                        positive_ai_response = clean_ai_response( positive_ai_response.strip())

                        log("------------------------------------------------------------------------------------------------------")
                        log("SD Prompt: \nPrompt: " + client_config.positive_prompt + " " + positive_ai_response + "\nNegative Prompt: " + client_config.negative_prompt)
                        log("------------------------------------------------------------------------------------------------------") 

                        log("Generateing image")
                        log("")
                        tti_image = generate_image(positive_ai_response,client_config,SD_WIDTH,SD_HEIGHT)
                        tti_image_s = downscale_image(tti_image, 0.4)

                        await websocket.send("SendingImaeg")
                        await websocket.send("IMAGE_START")
                        await send_image(websocket,IMAGE_DIR,tti_image_s)
              
                    await websocket.send("REQUEST_COMPLETE")
                    log("Request complete")
                    log("")

                else:
                    log("ERROR: NO role." + client_config.role)
                    log("")

            elif message == "FORCE_IMAGE": #image from chat history
            
                # PROMPT
                        prompt = build_prompt_simple_and_history(read_text_file(PROMPT_DIR, client_config.text_to_image_config),client_config) # text_to_image_config and hystory to the llm

                        # KOBOLD
                        ai_response = ""

                        async for token in stream_kobold(prompt):
                            ai_response += token


                        ai_response = clean_ai_response( ai_response.strip())
             

                        positive_ai_response = ""
                        positive_prompt = build_prompt_simple(read_text_file(PROMPT_DIR, client_config.Positive_Prompt_Extractor_Config) + " " + ai_response)

                        async for token in stream_kobold(positive_prompt):
                            positive_ai_response += token

                        positive_ai_response = clean_ai_response( positive_ai_response.strip())
                        
                        log("")
                        log("Genorating Image.")
                        log("")
                        await websocket.send("GenoratingImage")

                        # TTI           
                        tti_image = generate_image(positive_ai_response,client_config,SD_WIDTH,SD_HEIGHT)
                        
                        log("")
                        log("Downscaling Image: " + tti_image)
                        log("")

                        # convert image for esp32
                        tti_image_s = downscale_image(tti_image, 0.4)

                        log("")
                        log("Sending Image: " + tti_image_s)
                        log("")

                        await websocket.send("SendingImaeg")
                        await websocket.send("IMAGE_START")
                        await send_image(websocket,IMAGE_DIR,tti_image_s)

                        log("Request complete")
                        await websocket.send("REQUEST_COMPLETE")
                        log("")

            elif message == "CLEAR_HISTORY":
                conversation_history.clear()
                log("Conversation history cleared.")
                log("")

    except Exception as e:

        log( "Client disconnected")
        log(str(e))
        log("")

# ==================================================
# MAIN
# ==================================================

async def main():

    wait_for_services()

    log(
        f"Starting server on "
        f"ws://{HOST}:{PORT}"
    )
    log("")

    async with websockets.serve(

        handle_client,

        HOST,

        PORT,

        max_size=20_000_000,

        ping_interval=None,

        compression=None
    ):

        log("Server ready")
        log("")
        await asyncio.Future()

asyncio.run(main())

