---
title: 'Volume 5: Model Porting'
description: Convert, upload, and integrate third-party models into CosmoEdge scenario tasks — a complete end-to-end walkthrough.
prev:
  text: 'Volume 4: Pipeline Orchestration'
  link: /en/tutorials/04-pipeline-orchestration/pipeline-orchestration
next: false
---

# Volume 5: Model Porting

> **Estimated time**: 30–45 minutes
> **Goal**: Learn to convert, upload, and integrate a third-party model into the system — completing a full loop from model porting to scenario task integration to result verification
> **Prerequisites**: Completed CosmoEdge Scenario Task Orchestration; understand basic pipeline orchestration concepts
> **Extra environment needed**: A computer with Docker installed for model conversion
>

**CosmoEdge Scenario Task Orchestration** covers "how to organize existing capabilities." This volume covers "how to bring in new ones." In other words:

+ **CosmoEdge Scenario Task Orchestration** teaches you to orchestrate pipelines
+ **CosmoEdge Third-Party Model Porting** teaches you to introduce third-party models

After completing this volume, you'll be able to:

+ Convert an ONNX model to a device-runnable `bmodel`
+ Upload the model to CosmoEdge
+ Configure model metadata and verify the model works
+ Integrate the third-party model into a new scenario task

## Learning Path
```plain
Prepare the model
  ↓
Convert inside Docker
  ↓
Upload to the system and fill in metadata
  ↓
Validate with images
  ↓
Integrate into a scenario task and run end-to-end testing
```

## Chapter 1: Prepare the Third-Party Model
This volume uses a publicly available **VisDrone detection model** as its example.

::: warning Model Repository Link
[VisDrone-YOLOv8-Models-Upgrade](https://github.com/superbabiiX/VisDrone-YOLOv8-Models-Upgrade/tree/main/visdrone%20models)
:::

We chose it for three reasons:

1. It's public and reusable — perfect for a tutorial example.
2. It's a standard object detection task, making results easy to verify.
3. Its categories differ from the built-in scenarios, proving the platform supports more than just bundled models.

### 1.1 Supported Input Formats
| Format | Notes |
| --- | --- |
| ONNX | Recommended — best compatibility |
| PyTorch `.pt` | Must be exported to ONNX first |
| TensorFlow `.pb` | Must be exported to ONNX first |


> **Recommendation**
>
> For your first porting attempt, go with ONNX.
> This keeps the focus on "model conversion and integration" rather than framework export quirks.
>

### 1.2 Example Files for This Volume
Prepare an ONNX model file, for example:

```plain
visdrone_yolov8s.onnx
```

Place it in a local directory, such as:

```plain
/home/user/models/
```

> **Note**
>
> If you substitute your own model later, you'll need to update the filenames, input dimensions, and output names in the commands below accordingly.
>

## Chapter 2: Docker Environment Setup
Model conversion uses an official Docker image provided by Sophon (SOPHGO).

### 2.1 Install Docker
| Operating System | Installation Method |
| --- | --- |
| Ubuntu / Debian | `sudo apt-get install docker.io` |
| CentOS | `sudo yum install docker` |
| Windows | Install [Docker Desktop](https://www.docker.com/products/docker-desktop/) |


<!-- Screenshot of Docker Desktop -->
![](images/img_01.png)

Verify the installation:

```bash
docker --version
## Expected output: Docker version 2x.x.x
```

<!-- Screenshot of version output -->
![](images/img_02.png)



Add your user to the docker group:

```plain
sudo gpasswd -a USER docker
```

Replace `USER` with your actual username.



Update the group:

```plain
newgrp docker
```

<!-- Screenshot of group update -->
![](images/img_03.png)

### 2.2 Pull the Image
Download the image archive:

```plain
wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/24/06/14/12/sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
```

<!-- Screenshot of download -->
![](images/img_04.png)



Load the image:

```plain
docker load -i sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
```

<!-- Screenshot of loading -->
![](images/img_05.png)

<!-- Screenshot of load complete -->
![](images/img_06.png)

### 2.3 Useful Docker Commands
| Command | Purpose |
| --- | --- |
| `docker images` | List local images |
| `docker run -it -v /local/path:/container/path sophgo/tpuc_dev:latest bash` | Start a container with a mounted directory |
| `exit` | Exit the container |


## Chapter 3: Model Conversion
Converting an ONNX model to a device-runnable `bmodel` takes just two core steps:

1. `model_transform`
2. `model_deploy`

### 3.1 Start the Container
```bash
docker run -it -v /home/user/models:/workspace sophgo/tpuc_dev:v3.2 bash
```

This command:

+ Starts the official conversion environment
+ Mounts your local model directory to `/workspace` inside the container

<!-- Screenshot of container start -->
![](images/img_07.png)

### 3.2 Install Conversion Dependencies
```bash
pip install tpu_mlir -i https://mirrors.ustc.edu.cn/pypi/simple/
```

<!-- Screenshot of pip install -->
![](images/img_08.png)

### 3.3 Step 1: Convert to MLIR
```bash
model_transform \
  --model_name visdrone_yolov8s \
  --model_def /workspace/visdrone_yolov8s.onnx \
  --input_shapes [[1,3,640,640]] \
  --pixel_format rgb \
  --output_names output0 \
  --mlir visdrone.mlir
```

<!-- Screenshots of conversion process -->
![](images/img_09.png)

<!-- Screenshot of conversion complete -->
![](images/img_10.png)

### 3.4 Step 2: Convert to bmodel
```bash
model_deploy \
  --mlir visdrone.mlir \
  --quantize F16 \
  --chip bm1688 \
  --model visdrone_yolov8s_f16.bmodel
```

<!-- Screenshot of bmodel conversion -->
![](images/img_11.png)

After conversion, the `/workspace` directory will contain:

```plain
visdrone_yolov8s_f16.bmodel
```

<!-- Screenshot of output file -->
![](images/img_12.png)

### 3.4 Three Key Points to Watch
1. `--input_shapes` must match the model's training/export configuration.
2. `--pixel_format` must match the preprocessing used during training.
3. `--output_names` must match the actual output tensor names from the model export.


## Chapter 4: Upload the Model to CosmoEdge
### 4.1 Open the Model Repository
1. Navigate to **Model Repository**.

<!-- Screenshot of model repository -->
![](images/img_13.png)

2. Click **Add Model**.

<!-- Screenshot of add model button -->
![](images/img_14.png)



<!-- Screenshot of add model dialog -->
![](images/img_15.png)

Fill in the model information:

| Field | Example Value | Notes |
| --- | --- | --- |
| Primary Type | Detection Algorithm |  |
| Sub Type | yolov8_det | Matches the model's YOLO version |
| Model Name | VisDrone Drone Detection | For easy identification |
| Upload File | `visdrone_yolov8s_f16.bmodel` | The converted model file |
| Normalization | 0–1 |  |
| Color Channel | RGB |  |
| Class Labels | pedestrian, people, bicycle, car, van, truck, tricycle, awning-tricycle, bus, motor | For category display and mapping |




Click **OK** to save.

### 4.2 Post-Upload Checklist
After uploading, verify:

+ The model appears in the Model Repository list.
+ Status shows as normal.
+ Name, type, input dimensions, and labels are all correct.

<!-- Screenshot of model in repository -->
![](images/img_16.png)

Edit configuration — input dimensions, confidence thresholds, and other basic parameters:

<!-- Screenshot of model config -->
![](images/img_17.png)



Edit class information:

<!-- Screenshot of class config -->
![](images/img_18.png)



> Accurate model metadata is critical. Errors here are difficult to debug later.
>

## Chapter 5: Validate the Model with Image Testing
Before integrating the model into a scenario task pipeline, run a lightweight validation first.

### 5.1 Prepare Test Images
Gather several images that match the model's target domain — for example, aerial drone-view photographs.

### 5.2 Create an Image Validation Task
1. Go to **Scenario Tasks** and create a new drone image detection task.

<!-- Screenshot of scenario tasks list -->
![](images/img_19.png)

<!-- Screenshot of new task dialog -->
![](images/img_20.png)

<!-- Screenshot of task saved -->
![](images/img_21.png)



2. Create the pipeline for **Drone Image Detection**.

Click **Pipeline Orchestration** to enter the orchestration page.

<!-- Screenshot of pipeline orchestration -->
![](images/img_22.png)



Add an **Object Detection** node.

<!-- Screenshot of detection node added -->
![](images/img_23.png)



Configure the **business logic parameters**:

<!-- Screenshot of config panel -->
![](images/img_24.png)

+ Base Model: VisDrone Drone Detection
+ Select Labels: All selected



Click **Save** to store the pipeline.



### 5.3 Image Analysis
1. Select the **Drone Image Detection** algorithm.

<!-- Screenshot of algorithm selection -->
![](images/img_25.png)

2. Upload images.

<!-- Screenshot of image upload -->
![](images/img_26.png)



3. Click **Start Analysis** to begin testing.

<!-- Screenshot of analysis results -->
![](images/img_27.png)



### 5.3 Results Analysis
<!-- Screenshot of detailed results -->
![](images/img_28.png)



Results analysis:

+ Pedestrians, vehicles, and other targets in the image are successfully detected.
+ Bounding box positions are generally accurate.
+ Class names display correctly — not as numbers or blank labels.



> If image validation doesn't pass, don't rush into pipeline integration.
> Go back and check:
>
> + Was the model file uploaded correctly?
> + Do the input dimensions match?
> + Are all class labels configured properly?
>



## Chapter 6: Integrate the Third-Party Model into a Scenario Task
Now let's integrate this model into an actual business workflow.

### Scenario Description
Create a new **Drone Object Detection** scenario task:

+ Detect pedestrians and vehicles in the frame
+ Generate events
+ Display bounding boxes and class labels on the live feed

### 6.1 Create a New Algorithm
1. Go to **Scenario Tasks** → **New Task**.

<!-- Screenshot of new task dialog -->
![](images/img_29.png)

Fill in the basic information:

+ Task Name: Drone Object Detection
+ Data Source Type: Video Analysis
+ Task Type: Detection/Analysis



<!-- Screenshot of saved task -->
![](images/img_30.png)

### 6.2 Build the Minimum Viable Pipeline
Add nodes in this order:

```plain
Video Decode → Object Detection (third-party model) → Object Tracking → Region Alarm Judgment → Event Reporting
```

1. Click **Pipeline Orchestration** to enter the orchestration page.

<!-- Screenshot of empty pipeline -->
![](images/img_31.png)



2. Add the orchestration logic:

**Step 1**: Add **Video Decode**.


**Step 2**: Add **Object Detection**.

<!-- Screenshot of detection node config -->
![](images/img_32.png)



**Step 3**: Add **Object Tracking**.

<!-- Screenshot of tracking node config -->
![](images/img_33.png)

**Step 4**: Add **Region Alarm Judgment**.

<!-- Screenshot of region judgment -->
![](images/img_34.png)



**Step 5**: Add **Event Reporting**.

<!-- Screenshot of event reporting config -->
![](images/img_35.png)



3. Configure parameters

<!-- Screenshot of parameter config -->
![](images/img_36.png)

4. Click **Save** to store the pipeline.



## Chapter 7: End-to-End Verification
### 7.1 Create a Video Source
1. Prepare a drone-perspective test video.
2. Go to **Video Sources** and add a video channel.

<!-- Screenshot of video channel creation -->
![](images/img_37.png)

Parameters:

+ Source Type: Offline Video
+ Channel Name: Drone Camera
+ Upload Video: drone.mp4



3. Configure **Service Assignment**.

Click **Service Assignment** to open the configuration page.

<!-- Screenshot of service assignment -->
![](images/img_38.png)



Select **Drone Object Detection** as the scenario task.

<!-- Screenshot of task selection -->
![](images/img_39.png)



Add a detection region.

<!-- Screenshot of region added -->
![](images/img_40.png)



Configure **Runtime Strategy** — set play count to 0.

<!-- Screenshot of runtime strategy -->
![](images/img_41.png)


Click **Save** to save and start.

### 7.2 Check the Live Preview
Go to **Live Preview** and select the Drone Camera channel.

<!-- Screenshot of live preview -->
![](images/img_42.png)



Enable the algorithm overlay: Drone Object Detection.

<!-- Screenshot of overlay enabled -->
![](images/img_43.png)

Alarm popup:

![](images/img_44.png)

Verify:

+ Bounding boxes appear in the frame.
+ Class labels display correctly.
+ Different targets maintain consistent tracking.

### 7.3 Check Alarm Records
Go to **Event Center** → **Detection/Analysis** to review alarm event records.

<!-- Screenshot of alarm records -->
![](images/img_45.png)

Results analysis: Alarm events are recorded normally, confirming the pipeline is working correctly.

### 7.4 Success Criteria
If all three of the following are true, your third-party model porting is complete:

1. Image testing produces correct results.
2. The scenario task starts successfully.
3. Both live preview and alarm records show expected output.

## Appendix
### A. Common Issues and Troubleshooting
| Problem | Possible Cause | Solution |
| --- | --- | --- |
| Model uploaded but no detections | Input dimensions configured incorrectly | Verify model metadata matches conversion parameters |
| Classes show as numbers | Class labels not configured | Add the label mapping in Model Repository |
| Scenario task starts but no bounding boxes | Missing OSD overlay node | Add an OSD overlay at the end of the pipeline |
| Image testing works but video results are poor | Test video distribution differs significantly from training data | Try a video that better matches the model's training domain |
| Service fails to start | Nodes not properly connected or model type mismatch | Check pipeline structure and model type settings |


### What's Next
After completing all five volumes, you now have a complete capability chain:

```plain
Device setup
  ↓
Configure built-in scenarios
  ↓
Define large model rules
  ↓
Orchestrate business pipelines
  ↓
Port third-party models
```

This means you can:

+ Rapidly deploy scenarios using built-in capabilities
+ Fill long-tail gaps with VLM / DINO
+ Organize capabilities into business workflows through orchestration
+ Bring your own models into the system
