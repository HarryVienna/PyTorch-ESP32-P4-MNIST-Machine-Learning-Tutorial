import os
import time
import torch
import torch.nn as nn
import torch.optim as optim
import torch.nn.functional as F
from torchvision import datasets, transforms
from torch.utils.data import DataLoader, ConcatDataset

# ==============================================================================
# CONFIGURATION
# ==============================================================================
# Choose model: "simple" or "advanced" or "classic"
MODEL_TYPE = "advanced"

BATCH_SIZE = 64
LEARNING_RATE = 0.001
EPOCHS = 1

# Folder for MNIST
MNIST_DATA_PATH = "./mnist_data"

# Folder for your hand-drawn images (optional)
CUSTOM_DATA_PATH = "./custom_data"

# ONNX file
OUTPUT_PATH = "./output_models"


# ==============================================================================
# 1. DEFINE MODELS
# ==============================================================================

# --- OPTION A: Simple and fast ---
class SimpleCNN(nn.Module):
    def __init__(self):
        super(SimpleCNN, self).__init__()
        # Input: 1 Channel (Grayscale), 28x28
        self.conv1 = nn.Conv2d(1, 8, kernel_size=3, padding=1)
        self.conv2 = nn.Conv2d(8, 16, kernel_size=3, padding=1)
        self.pool = nn.MaxPool2d(2, 2)
        # 28x28 -> Pool -> 14x14 -> Pool -> 7x7.
        # 16 Filters * 7 * 7 Pixels = 784 Inputs for FC
        self.fc1 = nn.Linear(16 * 7 * 7, 10)

    def forward(self, x):
        x = self.pool(F.relu(self.conv1(x)))
        x = self.pool(F.relu(self.conv2(x)))
        x = x.view(-1, 16 * 7 * 7)  # Flatten
        x = self.fc1(x)
        return x


## --- OPTION B: Complex and exact ---
class AdvancedCNN(nn.Module):
    def __init__(self):
        super(AdvancedCNN, self).__init__()

        # Layer 1: More Filters + BatchNorm
        self.conv1 = nn.Conv2d(1, 32, kernel_size=3, padding=1)
        self.bn1 = nn.BatchNorm2d(32)

        # Layer 2: Even more Filters
        self.conv2 = nn.Conv2d(32, 64, kernel_size=3, padding=1)
        self.bn2 = nn.BatchNorm2d(64)

        self.pool = nn.MaxPool2d(2, 2)

        # Dropout to prevent overfitting (memorization)
        self.dropout = nn.Dropout(0.5)

        # Two-stage Fully Connected Layer
        self.fc1 = nn.Linear(64 * 7 * 7, 128)
        self.fc2 = nn.Linear(128, 10)

    def forward(self, x):
        # Block 1
        x = self.conv1(x)
        x = self.bn1(x)
        x = F.relu(x)
        x = self.pool(x)

        # Block 2
        x = self.conv2(x)
        x = self.bn2(x)
        x = F.relu(x)
        x = self.pool(x)

        # Flatten
        x = x.view(-1, 64 * 7 * 7)

        # Classification Head
        x = self.dropout(x)
        x = self.fc1(x)
        x = F.relu(x)
        x = self.fc2(x)
        return x

# --- OPTION C: Classic Multi Layer Perceptron ---
class ClassicANN(nn.Module):
    def __init__(self):
        super(ClassicANN, self).__init__()

        # Input: 28x28 Pixels = 784 Inputs
        self.fc1 = nn.Linear(28 * 28, 128)

        # Hidden Layer
        self.fc2 = nn.Linear(128, 64)

        # Output: 10 Classes (Digits 0 to 9)
        self.fc3 = nn.Linear(64, 10)

    def forward(self, x):
        # 1. Destroy image structure and flatten into a long vector
        x = x.view(-1, 28 * 28)

        ## 2. Pass through linear layers
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        x = self.fc3(x)

        return x

# Helper to switch models
def get_model(type_name):
    if type_name == "advanced":
        return AdvancedCNN()
    elif type_name == "simple":
        return SimpleCNN()
    elif type_name == "classic":
        return ClassicANN()
    else:
        raise ValueError("Unknown model. Please choose 'advanced', 'simple' or 'classic'.")


# ==============================================================================
# 2. LOAD DATA (WITH CUSTOM DATA SUPPORT)
# ==============================================================================
def get_dataloaders():
    # Standard normalization for MNIST (Grayscale!)
    transform = transforms.Compose([
        transforms.RandomRotation(10),  # Bisschen wackeln hilft immer
        transforms.RandomAffine(0, translate=(0.1, 0.1)),
        transforms.ToTensor(),
        transforms.Normalize((0.1307,), (0.3081,))
    ])

    print("--> Loading MNIST dataset...")
    mnist_data = datasets.MNIST(MNIST_DATA_PATH, train=True, download=True, transform=transform)

    # Prüfen auf eigene Daten (z.B. deutsche 1 und 7)
    if os.path.exists(CUSTOM_DATA_PATH):
        print(f"--> Custom data found in '{CUSTOM_DATA_PATH}'!")
        # Same transform, but force Grayscale (since PNG is often RGB)
        custom_transform = transforms.Compose([

            transforms.Grayscale(num_output_channels=1),
            transforms.RandomRotation(10),
            transforms.RandomAffine(0, translate=(0.1, 0.1)),
            transforms.Resize((28, 28)),  # Sicher ist sicher
            transforms.ToTensor(),
            transforms.Normalize((0.1307,), (0.3081,))
        ])

        custom_data = datasets.ImageFolder(root=CUSTOM_DATA_PATH, transform=custom_transform)

        # Dataset Boosting: Multiply own images by 50 so they carry weight
        # Otherwise, 50 images would be lost against 60,000 MNIST images.
        combined_dataset = ConcatDataset([mnist_data] + [custom_data] * 50)
        print(f"--> Training with {len(combined_dataset)} images (MNIST + Custom).")
    else:
        print("--> No custom data found. Training only with MNIST.")
        combined_dataset = mnist_data

    train_loader = DataLoader(combined_dataset, batch_size=BATCH_SIZE, shuffle=True, num_workers=2)

    # Test data (always pure MNIST for a fair comparison)
    test_dataset = datasets.MNIST(MNIST_DATA_PATH, train=False, transform=transforms.Compose([
        transforms.ToTensor(),
        transforms.Normalize((0.1307,), (0.3081,))
    ]))
    test_loader = DataLoader(test_dataset, batch_size=1000, shuffle=False, num_workers=2)

    return train_loader, test_loader


# ==============================================================================
# 3. TRAINING LOOP
# ==============================================================================
def train(model, device, train_loader, optimizer, epoch):
    model.train()  # Wichtig für Dropout und BatchNorm!
    for batch_idx, (data, target) in enumerate(train_loader):
        data, target = data.to(device), target.to(device)
        optimizer.zero_grad()
        output = model(data)
        loss = F.cross_entropy(output, target)
        loss.backward()
        optimizer.step()

        if batch_idx % 100 == 0:
            print(
                f'Train Epoch: {epoch} [{batch_idx * len(data)}/{len(train_loader.dataset)}]\tLoss: {loss.item():.6f}')


def test(model, device, test_loader):
    model.eval()  # Important: Turns off Dropout!
    test_loss = 0
    correct = 0
    with torch.no_grad():
        for data, target in test_loader:
            data, target = data.to(device), target.to(device)
            output = model(data)
            test_loss += F.cross_entropy(output, target, reduction='sum').item()
            pred = output.argmax(dim=1, keepdim=True)
            correct += pred.eq(target.view_as(pred)).sum().item()

    test_loss /= len(test_loader.dataset)
    accuracy = 100. * correct / len(test_loader.dataset)
    print(
        f'\nTest set: Average loss: {test_loss:.4f}, Accuracy: {correct}/{len(test_loader.dataset)} ({accuracy:.2f}%)\n')
    return accuracy


# ==============================================================================
# 4. MAIN & EXPORT
# ==============================================================================
def main():
    # --- TIME MEASUREMENT START ---
    start_time = time.time()

    if not os.path.exists(OUTPUT_PATH):
        os.makedirs(OUTPUT_PATH)

    use_cuda = torch.cuda.is_available()
    device = torch.device("cuda" if use_cuda else "cpu")
    print(f"Using hardware: {device}")
    print(f"Model type: {MODEL_TYPE.upper()}")

    # Disable MIOpen (via the cudnn flag) (For AMD Strix Halo)
    torch.backends.cudnn.enabled = False

    # Load data
    train_loader, test_loader = get_dataloaders()

    # Initialize model
    model = get_model(MODEL_TYPE).to(device)
    optimizer = optim.Adam(model.parameters(), lr=LEARNING_RATE)

    # Start training
    for epoch in range(1, EPOCHS + 1):
        train(model, device, train_loader, optimizer, epoch)
        test(model, device, test_loader)


    print("\n==================================================")
    print(" TRAINING FINISHED - STARTING EXPORT & QUANTIZATION")
    print("==================================================")

    model.eval()

    # =========================================================
    # ONNX EXPORT
    # =========================================================
    import onnxscript

    ONNX_FILENAME = f"mnist_{MODEL_TYPE}.onnx"
    onnx_full_path = os.path.join(OUTPUT_PATH, ONNX_FILENAME)
    print(f"-> Exporting safe ONNX format to: {onnx_full_path}")

    dummy_input = torch.randn(1, 1, 28, 28).to(device)
    torch.onnx.export(
        model,
        dummy_input,
        onnx_full_path,
        export_params=True,
        do_constant_folding=True,
        input_names=['input'],
        output_names=['output']
    )

    # =========================================================
    # ESP32 QUANTIZATION
    # =========================================================
    from esp_ppq import TargetPlatform
    from esp_ppq.api import quantize_onnx_model, export_ppq_graph

    print("\n--> Preparing ONNX quantization...")

    # Get real calibration images
    calib_images, _ = next(iter(test_loader))

    # PPQ ONNX interface often expects the input images exactly in the format
    # that the model receives. We extract 32 individual images.
    calib_dataloader = [calib_images[i:i + 1] for i in range(32)]

    print("--> Starting PPQ quantization (Target: ESP_DL_INT8)...")
    quantized_model = quantize_onnx_model(
        onnx_import_file=onnx_full_path,  # <--- WIR GEBEN IHM DIREKT DAS ONNX!
        calib_dataloader=calib_dataloader,
        calib_steps=32,
        input_shape=[1, 1, 28, 28],
        platform=TargetPlatform.ESPDL_INT8,
        setting=None
    )

    # DIRECT ESPDL EXPORT
    ESPDL_FILENAME = f"mnist_{MODEL_TYPE}_cnn.espdl"
    print(f"\n--> Exporting finished ESP32 file: {ESPDL_FILENAME}")

    export_ppq_graph(
        graph=quantized_model,
        platform=TargetPlatform.ESPDL_INT8,
        graph_save_to=os.path.join(OUTPUT_PATH, ESPDL_FILENAME),
        config_save_to=os.path.join(OUTPUT_PATH, ESPDL_FILENAME + ".json")
    )

    # --- TIME MEASUREMENT END ---
    end_time = time.time()
    duration = end_time - start_time
    mins = int(duration // 60)
    secs = int(duration % 60)

    print("\n--------------------------------------------------")
    print(" ALL DONE!")
    print(f" Total duration: {mins} minutes and {secs} seconds")
    print(f" Your finished file for the ESP32 is located here: {os.path.join(OUTPUT_PATH, ESPDL_FILENAME)}")
    print("--------------------------------------------------")


if __name__ == '__main__':
    main()