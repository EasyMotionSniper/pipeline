import requests
import hashlib
import time
import json

def send_webhook(name, url, secret):
    payload = {
        "name": name
    }
    
    timestamp = str(int(time.time()))
    data_str = json.dumps(payload, separators=(',', ':'))
    signature_base = f"{timestamp}.{data_str}.{secret}"
    print(f"signature_base: {signature_base}")
    signature = hashlib.sha256(signature_base.encode()).hexdigest()
    
    headers = {
        "Content-Type": "application/json",
        "X-Webhook-Timestamp": timestamp,
        "X-Webhook-Signature": signature
    }
    
    try:
        response = requests.post(
            url,
            data=json.dumps(payload),
            headers=headers,
            timeout=10
        )
        
        print(f"status_code: {response.status_code}")
        print(f"response_text: {response.text}")
        
        return response.status_code == 200
        
    except requests.exceptions.RequestException as e:
        print(f"send_webhook_error: {e}")
        return False

if __name__ == "__main__":
    WEBHOOK_URL = "http://localhost:8080/webhook"  
    WEBHOOK_SECRET = "shared_secret"          
    NAME_TO_SEND = "example"                       
    
    send_webhook(NAME_TO_SEND, WEBHOOK_URL, WEBHOOK_SECRET)
