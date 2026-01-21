import socket
import threading
import tkinter as tk 
from tkinter import scrolledtext ,messagebox, simpledialog 

class ChatGUI:
    def __init__(self):
        #intial 
        self.host = "127.0.0.1"
        self.port = 5000
        self.sock = socket.socket(socket.AF_INET,socket.SOCK_STREAM)

        #login 
        self.root = tk.Tk()
        self.root.withdraw()

        self.username = simpledialog.askstring("Username","What is your name?",parent = self.root)
        if not self.username:
            self.root.destroy()
            return 
        #connecting to C++
        try:
            self.sock.connect((self.host,self.port))
            #sending the first message 
            self.sock.send(self.username.encode('utf-8'))
        except Exception as e :
            messagebox.showerror("Connect Error",f"Could not connect to server:{e}")
            self.root.destroy()
            return 
        
        #Main Window
        self.root.deiconify()  #main window dispaly
        self.root.title(f"P2P Chat - {self.username}")
        self.root.geometry("600x500")

        #chat history
        self.chat_history = scrolledtext.ScrolledText(self.root,state='disabled',wrap=tk.WORD)
        self.chat_history.pack(padx = 10,pady=10,fill = tk.BOTH,expand =True)
        self.chat_history.tag_config('server',foreground='blue',font=('Arial',10,'italic'))

        #input Frame
        input_frame = tk.Frame(self.root)
        input_frame.pack(fill=tk.X,padx=10,pady=10)

        self.message_entry = tk.Entry(input_frame)
        self.message_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 10))
        self.message_entry.bind("<Return>",lambda e: self.send_message)

        send_button = tk.Button(input_frame,text="Send",command = self.send_message,width=10,bg="#4CAF50",fg="white")
        send_button.pack(side = tk.RIGHT)

        #starting recieve thread to get the messages 
        self.active  = True
        self.recv_thread = threading.Thread(target=self.recieve_loop,daemon = True)
        self.recv_thread.start()

        self.root.protocol("WM_DELETE_WINDOW",self.on_close)
        self.root.mainloop()

    def send_message(self):
        msg = self.message_entry.get().strip()
        if msg:
            try:
                self.sock.send(msg.encode('utf-8'))
                self.message_entry.delete(0,tk.END)
                if msg == "exit":
                    self.on_close()
            except:
                messagebox.showerror("Error","Lost connection to server")
                self.on_close()

    def recieve_loop(self):
        while(self.active):
            try:
                data = self.sock.recv(1024).decode('utf-8')
                if data:
                    self.update_chat(data)
                else:
                    break
            except:
                break

    def update_chat(self,msg):
        self.chat_history.config(state='normal')

        if "[SERVER]" in msg or "joined" in msg:
            self.chat_history.insert(tk.END,msg+"\n",'server')
        else:
            self.chat_history.insert(tk.END, msg + "\n")

        self.chat_history.config(state="disabled")
        self.chat_history.yview(tk.END)
    
    def on_close(self):
        self.active = False
        self.sock.close()
        self.root.destroy()

if __name__=="__main__":
    ChatGUI()

