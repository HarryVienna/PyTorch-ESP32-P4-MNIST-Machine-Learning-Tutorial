# PyTorch ESP32 P4 MNIST Machine Learning Tutorial

This tutorial describes real time digit recognition on the ESP32 P4. The process includes the creation of custom MNIST training data, training a CNN with PyTorch, and Int8 quantization using ESP PPQ. 
Afterwards, the finished model runs directly on a CrowPanel Advance 7" ESP32-P4 HMI AI Display. The result is a touch application that evaluates drawn digits and immediately visualizes the inference result as a bar chart.

<img width="800" height="438" alt="DSCF7885_small" src="https://github.com/user-attachments/assets/d82e5890-68a3-4d61-ad93-4853e6740dfe" />

---

## Project Structure

The repository consists of two main components:

* **Python MNIST:** Contains the script `train_mnist_esp32.py` for training the neural networks and exporting them.
* **ESP32 Project:** Contains the C++ source code for hardware execution, including display control.

---

## Prerequisites

The following packages are required to execute the Python scripts:

* Python 3.12
* PyTorch
* torchvision
* onnxscript
* esp_ppq

A working installation of the ESP IDF framework is required for hardware compilation.

---

## Functionality

### 1. Training and Quantization
The Python script loads the MNIST dataset and trains either a simple, complex, or classic neural network. Custom training data can be added via the `custom_data` folder. After training, the script exports the model to the ONNX format and quantizes it into a compact Int8 file (`.espdl`).

### 2. Hardware Execution
The generated `.espdl` file is loaded onto the ESP32 P4. The C++ code initializes the tensor, passes the inputs from the touch interface to the model, and executes the inference.

### 3. Display Visualization
The file `chart_display.c` offers two visualization modes for the display, which can be selected via the constant `CHART_MODE_RAW`. 

The first variant uses a softmax function to convert the network outputs into probabilities. Due to the exponential calculation, the highest individual value is strongly elevated, while the remaining outputs can drop to zero. The focus here is on making a clear decision for a single digit. 

The second variant uses a direct scaling of the output values. In this mode, the original relations of the prediction are preserved, and the bars show the actual weighting relative to each other. This method makes it very easy to see how certain or uncertain the network values are.

---

## Execution

1. Install the Python dependencies via the terminal.
2. Start the training via `python train_mnist_esp32.py`.
3. Copy the generated `.espdl` file from the output folder into the ESP32 project.
4. Compile the C++ project via the ESP IDF framework and flash it to the microcontroller.
