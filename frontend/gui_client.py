import socket
import threading
import tkinter as tk
from tkinter import scrolledtext, messagebox, simpledialog


class ChatGUI:
    def __init__(self):
        self.host = "127.0.0.1"
        self.port = 5000
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        self.root = tk.Tk()
        self.root.withdraw()

        # ---------- Username ----------
        self.username = simpledialog.askstring(
            "Username", "What is your name?", parent=self.root
        )

        if not self.username:
            self.root.destroy()
            return

        # ---------- Connect ----------
        try:
            self.sock.connect((self.host, self.port))
            self.sock.send((self.username + "\n").encode("utf-8"))
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))
            self.root.destroy()
            return

        # ---------- UI ----------
        self.root.deiconify()
        self.root.title(f"P2P Chat - {self.username}")
        self.root.geometry("600x500")

        self.chat_history = scrolledtext.ScrolledText(
            self.root, state="disabled", wrap=tk.WORD
        )
        self.chat_history.pack(padx=10, pady=10, fill=tk.BOTH, expand=True)

        self.chat_history.tag_config("dm", foreground="purple")

        input_frame = tk.Frame(self.root)
        input_frame.pack(fill=tk.X, padx=10, pady=10)

        self.message_entry = tk.Entry(input_frame)
        self.message_entry.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.message_entry.bind("<Return>", lambda e: self.send_message())

        send_button = tk.Button(
            input_frame, text="Send", command=self.send_message
        )
        send_button.pack(side=tk.RIGHT)

        # ---------- Networking ----------
        self.active = True
        threading.Thread(target=self.receive_loop, daemon=True).start()

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.mainloop()

    # ---------- Send ----------
    def send_message(self):
        msg = self.message_entry.get().strip()
        if msg:
            try:
                self.sock.send((msg + "\n").encode("utf-8"))
                self.update_chat(f"[You]: {msg}\n")
                self.message_entry.delete(0, tk.END)
            except:
                self.on_close()

    # ---------- Receive ----------
    def receive_loop(self):
        while self.active:
            try:
                data = self.sock.recv(1024).decode("utf-8")
                if not data:
                    break
                self.update_chat(data)
            except:
                break

    # ---------- Thread-safe UI update ----------
    def update_chat(self, msg):
        self.root.after(0, self._update_chat_safe, msg)

    def _update_chat_safe(self, msg):
        self.chat_history.config(state="normal")

        if msg.startswith("[You]"):
            self.chat_history.insert(tk.END,msg,"me")
        elif msg.startswith("[DM"):
            self.chat_history.insert(tk.END, msg, "dm")
        else:
            self.chat_history.insert(tk.END, msg)

        self.chat_history.config(state="disabled")
        self.chat_history.yview(tk.END)


    # ---------- Close ----------
    def on_close(self):
        self.active = False
        try:
            self.sock.close()
        except:
            pass
        self.root.destroy()


if __name__ == "__main__":
    ChatGUI()
