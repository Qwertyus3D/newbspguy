#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Gui.h"

void error_callback(int error, const char* description)
{
	printf("GLFW Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int g_scroll = 0;

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	g_scroll += round(yoffset);
}

Renderer::Renderer() {
	if (!glfwInit())
	{
		printf("GLFW initialization failed\n");
		return;
	}

	glfwSetErrorCallback(error_callback);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	window = glfwCreateWindow(1920, 1080, "bspguy", NULL, NULL);
	if (!window)
	{
		printf("Window creation failed\n");
		return;
	}

	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, key_callback);
	glfwSetScrollCallback(window, scroll_callback);

	glewInit();

	gui = new Gui(this);

	bspShader = new ShaderProgram(g_shader_multitexture_vertex, g_shader_multitexture_fragment);
	bspShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	bspShader->setMatrixNames(NULL, "modelViewProjection");

	colorShader = new ShaderProgram(g_shader_cVert_vertex, g_shader_cVert_fragment);
	colorShader->setMatrixes(&model, &view, &projection, &modelView, &modelViewProjection);
	colorShader->setMatrixNames(NULL, "modelViewProjection");
	colorShader->setVertexAttributeNames("vPosition", "vColor", NULL);

	g_render_flags = RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_SPECIAL 
		| RENDER_ENTS | RENDER_SPECIAL_ENTS | RENDER_POINT_ENTS | RENDER_WIREFRAME;
	
	pickInfo.valid = false;

	fgd = new Fgd(g_game_path + "/svencoop/sven-coop.fgd");
	fgd->parse();

	pointEntRenderer = new PointEntRenderer(fgd, colorShader);

	movingEnt = false;
	draggingAxis = -1;
	showDragAxes = true;
	gridSnapLevel = 0;
	gridSnappingEnabled = true;
	textureLock = false;
	transformMode = TRANSFORM_MOVE;

	copiedEnt = NULL;
	scaleVertsStart = NULL;
	scaleVerts = NULL;

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

	//cameraOrigin = vec3(-179, 181, 105);
	//cameraAngles = vec3(38, 0, 89);
}

Renderer::~Renderer() {
	glfwTerminate();
}

void Renderer::renderLoop() {
	glfwSwapInterval(1);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);

	{
		moveAxes.dimColor[0] = { 110, 0, 160 };
		moveAxes.dimColor[1] = { 0, 0, 220 };
		moveAxes.dimColor[2] = { 0, 160, 0 };
		moveAxes.dimColor[3] = { 160, 160, 160 };

		moveAxes.hoverColor[0] = { 128, 64, 255 };
		moveAxes.hoverColor[1] = { 64, 64, 255 };
		moveAxes.hoverColor[2] = { 64, 255, 64 };
		moveAxes.hoverColor[3] = { 255, 255, 255 };

		// flipped for HL coords
		moveAxes.model = new cCube[4];
		moveAxes.buffer = new VertexBuffer(colorShader, COLOR_3B | POS_3F, moveAxes.model, 6 * 6 * 4);
		moveAxes.numAxes = 4;
	}

	{
		scaleAxes.dimColor[0] = { 110, 0, 160 };
		scaleAxes.dimColor[1] = { 0, 0, 220 };
		scaleAxes.dimColor[2] = { 0, 160, 0 };

		scaleAxes.dimColor[3] = { 110, 0, 160 };
		scaleAxes.dimColor[4] = { 0, 0, 220 };
		scaleAxes.dimColor[5] = { 0, 160, 0 };

		scaleAxes.hoverColor[0] = { 128, 64, 255 };
		scaleAxes.hoverColor[1] = { 64, 64, 255 };
		scaleAxes.hoverColor[2] = { 64, 255, 64 };

		scaleAxes.hoverColor[3] = { 128, 64, 255 };		
		scaleAxes.hoverColor[4] = { 64, 64, 255 };
		scaleAxes.hoverColor[5] = { 64, 255, 64 };

		// flipped for HL coords
		scaleAxes.model = new cCube[6];
		scaleAxes.buffer = new VertexBuffer(colorShader, COLOR_3B | POS_3F, scaleAxes.model, 6 * 6 * 6);
		scaleAxes.numAxes = 6;
	}

	updateDragAxes();

	float s = 0.5f;
	cCube vertCube(vec3(-s, -s, -s), vec3(s, s, s), { 0, 128, 255 });
	VertexBuffer vertCubeBuffer(colorShader, COLOR_3B | POS_3F, &vertCube, 6 * 6);

	float lastFrameTime = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float frameDelta = glfwGetTime() - lastFrameTime;
		frameTimeScale = 0.05f / frameDelta;
		float fps = 1.0f / frameDelta;
		
		frameTimeScale = 144.0f / fps;

		lastFrameTime = glfwGetTime();

		controls();

		float spin = glfwGetTime() * 2;
		model.loadIdentity();
		model.rotateZ(spin);
		model.rotateX(spin);
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		setupView();
		bspShader->bind();
		glEnable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);

		for (int i = 0; i < mapRenderers.size(); i++) {
			model.loadIdentity();
			bspShader->updateMatrixes();

			int highlightEnt = -1;
			if (pickInfo.valid && pickInfo.mapIdx == i) {
				highlightEnt = pickInfo.entIdx;
			}
			mapRenderers[i]->render(highlightEnt);
		}

		model.loadIdentity();
		colorShader->bind();

		if (true) {
			if (pickInfo.valid && pickInfo.modelIdx > 0 && false) {
				//glDisable(GL_DEPTH_TEST);
				glDisable(GL_CULL_FACE);
				Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
				Entity* ent = map->ents[pickInfo.entIdx];

				static vector<vec3> test;
				static cCube* allVertCubes;
				static VertexBuffer* allBuff;

				if (test.empty()) {
					test = map->getModelPlaneIntersectVerts(pickInfo.modelIdx);
					allVertCubes = new cCube[test.size()];
					for (int i = 0; i < test.size(); i++) {
						vec3 asdf = test[i];
						asdf = vec3(asdf.x, asdf.z, -asdf.y);
						vec3 min = vec3(-s, -s, -s) + asdf;
						vec3 max = vec3(s, s, s) + asdf;
						allVertCubes[i] = cCube(min, max, {0, 128, 255});
					}
					allBuff = new VertexBuffer(colorShader, COLOR_3B | POS_3F, allVertCubes, 6 * 6*test.size());
					allBuff->upload();
					printf("%d intersection points\n", test.size());
				}

				model.loadIdentity();
				colorShader->updateMatrixes();
				allBuff->draw(GL_TRIANGLES);
			}

			colorShader->bind();
			model.loadIdentity();
			colorShader->updateMatrixes();
			drawLine(debugPoint - vec3(32, 0, 0), debugPoint + vec3(32, 0, 0), { 128, 128, 255 });
			drawLine(debugPoint - vec3(0, 32, 0), debugPoint + vec3(0, 32, 0), { 0, 255, 0 });
			drawLine(debugPoint - vec3(0, 0, 32), debugPoint + vec3(0, 0, 32), { 0, 0, 255 });
		}

		if (showDragAxes && !movingEnt && pickInfo.valid && pickInfo.entIdx > 0) {
			drawTransformAxes();
		}

		vec3 forward, right, up;
		makeVectors(cameraAngles, forward, right, up);
		//printf("DRAW %.1f %.1f %.1f -> %.1f %.1f %.1f\n", pickStart.x, pickStart.y, pickStart.z, pickDir.x, pickDir.y, pickDir.z);

		gui->draw();

		glfwSwapBuffers(window);
	}

	glfwTerminate();
}

void Renderer::drawTransformAxes() {
	glClear(GL_DEPTH_BUFFER_BIT);
	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	Entity* ent = map->ents[pickInfo.entIdx];

	updateDragAxes();

	glDisable(GL_CULL_FACE);

	if (transformMode == TRANSFORM_SCALE) {
		vec3 ori = scaleAxes.origin;
		model.translate(ori.x, ori.z, -ori.y);
		colorShader->updateMatrixes();
		scaleAxes.buffer->draw(GL_TRIANGLES);
	}
	if (transformMode == TRANSFORM_MOVE) {
		vec3 ori = moveAxes.origin;
		model.translate(ori.x, ori.z, -ori.y);
		colorShader->updateMatrixes();
		moveAxes.buffer->draw(GL_TRIANGLES);
	}
}

void Renderer::controls() {
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	for (int i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++) {
		pressed[i] = glfwGetKey(window, i) == GLFW_PRESS;
		released[i] = glfwGetKey(window, i) == GLFW_RELEASE;
	}

	anyCtrlPressed = pressed[GLFW_KEY_LEFT_CONTROL] || pressed[GLFW_KEY_RIGHT_CONTROL];
	anyAltPressed = pressed[GLFW_KEY_LEFT_ALT] || pressed[GLFW_KEY_RIGHT_ALT];
	anyShiftPressed = pressed[GLFW_KEY_LEFT_SHIFT] || pressed[GLFW_KEY_RIGHT_SHIFT];

	if (!io.WantCaptureKeyboard)
		cameraOrigin += getMoveDir() * frameTimeScale;

	moveGrabbedEnt();

	if (io.WantCaptureMouse)
		return;

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	vec2 mousePos(xpos, ypos);

	cameraContextMenus();

	cameraRotationControls(mousePos);

	makeVectors(cameraAngles, cameraForward, cameraRight, cameraUp);

	cameraObjectHovering();

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		bool transformingAxes = transformAxisControls();
		
		// object picking
		if (!transformingAxes && oldLeftMouse != GLFW_PRESS) {
			pickObject();
		}
	}
	else {
		if (draggingAxis != -1) {
			draggingAxis = -1;
			if (transformMode == TRANSFORM_SCALE) {
				updateScaleVerts(true);
				if (pickInfo.valid && pickInfo.modelIdx > 0) {
					Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
					map->vertex_manipulation_sync(pickInfo.modelIdx);
				}				
			}
		}
	}

	shortcutControls();

	oldLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	oldRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
	
	for (int i = GLFW_KEY_SPACE; i < GLFW_KEY_LAST; i++) {
		oldPressed[i] = pressed[i];
		oldReleased[i] = released[i];
	}

	oldScroll = g_scroll;
}

void Renderer::cameraRotationControls(vec2 mousePos) {
	// camera rotation
	if (draggingAxis == -1 && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
		if (!cameraIsRotating) {
			lastMousePos = mousePos;
			cameraIsRotating = true;
			totalMouseDrag = vec2();
		}
		else {
			vec2 drag = mousePos - lastMousePos;
			cameraAngles.z += drag.x * 0.5f;
			cameraAngles.x += drag.y * 0.5f;

			totalMouseDrag += vec2(fabs(drag.x), fabs(drag.y));

			cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);
			if (cameraAngles.z > 180.0f) {
				cameraAngles.z -= 360.0f;
			}
			else if (cameraAngles.z < -180.0f) {
				cameraAngles.z += 360.0f;
			}
			lastMousePos = mousePos;
		}

		ImGui::SetWindowFocus(NULL);
		ImGui::ClearActiveID();
	}
	else {
		cameraIsRotating = false;
		totalMouseDrag = vec2();
	}
}

void Renderer::cameraObjectHovering() {
	// axis handle hovering
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);
	hoverAxis = -1;
	if (showDragAxes && !movingEnt && pickInfo.valid && pickInfo.entIdx > 0) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo axisPick;
		memset(&axisPick, 0, sizeof(PickInfo));
		axisPick.bestDist = 9e99;

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		Entity* ent = map->ents[pickInfo.entIdx];
		vec3 origin = activeAxes.origin;

		int axisChecks = transformMode == TRANSFORM_SCALE ? activeAxes.numAxes : 3;
		for (int i = 0; i < axisChecks; i++) {
			if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[i], origin + activeAxes.maxs[i], axisPick.bestDist)) {
				hoverAxis = i;
			}
		}

		// center cube gets priority for selection (hard to select from some angles otherwise)
		if (transformMode == TRANSFORM_MOVE) {
			float bestDist = 9e99;
			if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[3], origin + activeAxes.maxs[3], bestDist)) {
				hoverAxis = 3;
			}
		}
	}
}

void Renderer::cameraContextMenus() {
	// context menus
	bool wasTurning = cameraIsRotating && totalMouseDrag.length() >= 1;
	if (draggingAxis == -1 && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE && oldRightMouse != GLFW_RELEASE && !wasTurning) {
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);

		PickInfo tempPick;
		memset(&tempPick, 0, sizeof(PickInfo));
		tempPick.bestDist = 9e99;
		for (int i = 0; i < mapRenderers.size(); i++) {
			if (mapRenderers[i]->pickPoly(pickStart, pickDir, tempPick)) {
				tempPick.mapIdx = i;
			}
		}

		if (tempPick.entIdx != 0 && tempPick.entIdx == pickInfo.entIdx) {
			gui->openContextMenu(pickInfo.entIdx);
		}
		else {
			gui->openContextMenu(-1);
		}
	}
}

void Renderer::moveGrabbedEnt() {
	// grabbing
	if (pickInfo.valid && movingEnt) {
		if (g_scroll != oldScroll) {
			float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 4.0f : 2.0f;
			if (pressed[GLFW_KEY_LEFT_CONTROL])
				moveScale = 1.0f;
			if (g_scroll < oldScroll)
				moveScale *= -1;

			grabDist += 16 * moveScale;
		}

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		vec3 delta = (cameraOrigin + cameraForward * grabDist) - grabStartOrigin;
		Entity* ent = map->ents[pickInfo.entIdx];

		vec3 oldOrigin = gragStartEntOrigin;
		vec3 offset = getEntOffset(map, ent);
		vec3 newOrigin = (oldOrigin + delta) - offset;
		vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

		ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
		mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
	}
}

void Renderer::shortcutControls() {
	// shortcuts
	if (pressed[GLFW_KEY_G] == GLFW_PRESS && oldPressed[GLFW_KEY_G] != GLFW_PRESS) {
		movingEnt = !movingEnt;
		if (movingEnt)
			grabEnt();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C]) {
		copyEnt();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_X] && !oldPressed[GLFW_KEY_X]) {
		cutEnt();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V]) {
		pasteEnt(false);
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_M] && !oldPressed[GLFW_KEY_M]) {
		gui->showTransformWidget = !gui->showTransformWidget;
	}
	if (anyAltPressed && pressed[GLFW_KEY_ENTER] && !oldPressed[GLFW_KEY_ENTER]) {
		gui->showKeyvalueWidget = !gui->showKeyvalueWidget;
	}
	if (pressed[GLFW_KEY_DELETE] && !oldPressed[GLFW_KEY_DELETE]) {
		deleteEnt();
	}
}

void Renderer::pickObject() {
	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	int oldEntIdx = pickInfo.entIdx;
	pickCount++;
	memset(&pickInfo, 0, sizeof(PickInfo));
	pickInfo.bestDist = 9e99;
	for (int i = 0; i < mapRenderers.size(); i++) {
		if (mapRenderers[i]->pickPoly(pickStart, pickDir, pickInfo)) {
			pickInfo.mapIdx = i;
		}
	}

	if (movingEnt && oldEntIdx != pickInfo.entIdx) {
		movingEnt = false;
	}

	updateScaleVerts(false);
}

bool Renderer::transformAxisControls() {
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	// axis handle dragging
	if (showDragAxes && !movingEnt && hoverAxis != -1 && draggingAxis == -1) {
		draggingAxis = hoverAxis;

		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		Entity* ent = map->ents[pickInfo.entIdx];

		axisDragEntOriginStart = getEntOrigin(map, ent);
		axisDragStart = getAxisDragPoint(axisDragEntOriginStart);
	}

	if (showDragAxes && !movingEnt && draggingAxis >= 0) {
		Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
		Entity* ent = map->ents[pickInfo.entIdx];

		activeAxes.model[draggingAxis].setColor(activeAxes.hoverColor[draggingAxis]);

		vec3 dragPoint = getAxisDragPoint(axisDragEntOriginStart);
		if (gridSnappingEnabled) {
			dragPoint = snapToGrid(dragPoint);
		}
		vec3 delta = dragPoint - axisDragStart;


		float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 2.0f : 1.0f;
		if (pressed[GLFW_KEY_LEFT_CONTROL] == GLFW_PRESS)
			moveScale = 0.1f;

		float maxDragDist = 8192; // don't throw ents out to infinity
		for (int i = 0; i < 3; i++) {
			if (i != draggingAxis % 3)
				((float*)&delta)[i] = 0;
			else
				((float*)&delta)[i] = clamp(((float*)&delta)[i] * moveScale, -maxDragDist, maxDragDist);
		}

		if (transformMode == TRANSFORM_MOVE) {
			vec3 offset = getEntOffset(map, ent);
			vec3 newOrigin = (axisDragEntOriginStart + delta) - offset;
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

			ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
			mapRenderers[pickInfo.mapIdx]->refreshEnt(pickInfo.entIdx);
		}
		else {
			if (ent->isBspModel() && delta.length() != 0) {

				vec3 scaleDirs[6]{
					vec3(1, 0, 0),
					vec3(0, 1, 0),
					vec3(0, 0, 1),
					vec3(-1, 0, 0),
					vec3(0, -1, 0),
					vec3(0, 0, -1),
				};

				scaleSelectedVerts(delta, scaleDirs[draggingAxis]);
				mapRenderers[pickInfo.mapIdx]->refreshModel(ent->getBspModelIdx());
			}
		}

		return true;
	}

	return false;
}

vec3 Renderer::getMoveDir()
{
	mat4x4 rotMat;
	rotMat.loadIdentity();
	rotMat.rotateX(PI * cameraAngles.x / 180.0f);
	rotMat.rotateZ(PI * cameraAngles.z / 180.0f);

	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);


	vec3 wishdir(0, 0, 0);
	if (pressed[GLFW_KEY_A])
	{
		wishdir -= right;
	}
	if (pressed[GLFW_KEY_D])
	{
		wishdir += right;
	}
	if (pressed[GLFW_KEY_W])
	{
		wishdir += forward;
	}
	if (pressed[GLFW_KEY_S])
	{
		wishdir -= forward;
	}

	wishdir *= moveSpeed;

	if (anyShiftPressed)
		wishdir *= 4.0f;
	if (anyCtrlPressed)
		wishdir *= 0.1f;
	return wishdir;
}

void Renderer::getPickRay(vec3& start, vec3& pickDir) {
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	// invert ypos
	ypos = windowHeight - ypos;

	// translate mouse coordinates so that the origin lies in the center and is a scaler from +/-1.0
	float mouseX = ((xpos / (double)windowWidth) * 2.0f) - 1.0f;
	float mouseY = ((ypos / (double)windowHeight) * 2.0f) - 1.0f;

	// http://schabby.de/picking-opengl-ray-tracing/
	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);

	vec3 view = forward.normalize(1.0f);
	vec3 h = crossProduct(view, up).normalize(1.0f); // 3D float vector
	vec3 v = crossProduct(h, view).normalize(1.0f); // 3D float vector

	// convert fovy to radians 
	float rad = fov * PI / 180.0f;
	float vLength = tan(rad / 2.0f) * zNear;
	float hLength = vLength * (windowWidth / (float)windowHeight);

	v *= vLength;
	h *= hLength;

	// linear combination to compute intersection of picking ray with view port plane
	start = cameraOrigin + view * zNear + h * mouseX + v * mouseY;

	// compute direction of picking ray by subtracting intersection point with camera position
	pickDir = (start - cameraOrigin).normalize(1.0f);
}

BspRenderer* Renderer::getMapContainingCamera() {
	for (int i = 0; i < mapRenderers.size(); i++) {
		Bsp* map = mapRenderers[i]->map;

		vec3 mins, maxs;
		map->get_bounding_box(mins, maxs);

		if (cameraOrigin.x > mins.x && cameraOrigin.y > mins.y && cameraOrigin.z > mins.z &&
			cameraOrigin.x < maxs.x && cameraOrigin.y < maxs.y && cameraOrigin.z < maxs.z) {
			return mapRenderers[i];
		}
	}
	return mapRenderers[0];
}

void Renderer::setupView() {
	fov = 75.0f;
	zNear = 1.0f;
	zFar = 262144.0f;

	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	glViewport(0, 0, windowWidth, windowHeight);

	projection.perspective(fov, (float)windowWidth / (float)windowHeight, zNear, zFar);

	view.loadIdentity();
	view.rotateX(PI * cameraAngles.x / 180.0f);
	view.rotateY(PI * cameraAngles.z / 180.0f);
	view.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);
}

void Renderer::addMap(Bsp* map) {
	BspRenderer* mapRenderer = new BspRenderer(map, bspShader, colorShader, pointEntRenderer);

	mapRenderers.push_back(mapRenderer);
}

void Renderer::drawLine(vec3 start, vec3 end, COLOR3 color) {
	cVert verts[2];

	verts[0].x = start.x;
	verts[0].y = start.z;
	verts[0].z = -start.y;
	verts[0].c = color;

	verts[1].x = end.x;
	verts[1].y = end.z;
	verts[1].z = -end.y;
	verts[1].c = color;

	VertexBuffer buffer(colorShader, COLOR_3B | POS_3F, &verts[0], 2);
	buffer.draw(GL_LINES);
}

vec3 Renderer::getEntOrigin(Bsp* map, Entity* ent) {
	vec3 origin = ent->hasKey("origin") ? parseVector(ent->keyvalues["origin"]) : vec3(0, 0, 0);
	return origin + getEntOffset(map, ent);
}

vec3 Renderer::getEntOffset(Bsp* map, Entity* ent) {
	if (ent->isBspModel()) {
		BSPMODEL& model = map->models[ent->getBspModelIdx()];
		return model.nMins + (model.nMaxs - model.nMins) * 0.5f;
	}
	return vec3(0, 0, 0);
}

void Renderer::updateDragAxes() {
	float baseScale = 1.0f;

	Bsp* map = NULL;
	Entity* ent = NULL;

	if (pickInfo.valid && pickInfo.entIdx > 0) {
		map = mapRenderers[pickInfo.mapIdx]->map;
		ent = map->ents[pickInfo.entIdx];
		baseScale = (getEntOrigin(map, ent) - cameraOrigin).length() * 0.005f;
	}

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_SCALE ? &scaleAxes : &moveAxes);

	float s = baseScale;
	float s2 = baseScale*2;
	float d = baseScale*32;

	if (transformMode == TRANSFORM_SCALE) {
		vec3 entMin, entMax;
		if (ent != NULL && ent->isBspModel()) {
			map->get_model_vertex_bounds(ent->getBspModelIdx(), entMin, entMax);
			vec3 modelOrigin = entMin + (entMax - entMin) * 0.5f;

			entMax -= modelOrigin;
			entMin -= modelOrigin;

			scaleAxes.origin = modelOrigin;
			if (ent->hasKey("origin")) {
				scaleAxes.origin += parseVector(ent->keyvalues["origin"]);
			}
		}

		vec3 axisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 axisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};
		
		scaleAxes.model[0] = cCube(axisMins[0], axisMaxs[0], scaleAxes.dimColor[0]);
		scaleAxes.model[1] = cCube(axisMins[1], axisMaxs[1], scaleAxes.dimColor[1]);
		scaleAxes.model[2] = cCube(axisMins[2], axisMaxs[2], scaleAxes.dimColor[2]);

		scaleAxes.model[3] = cCube(axisMins[3], axisMaxs[3], scaleAxes.dimColor[3]);
		scaleAxes.model[4] = cCube(axisMins[4], axisMaxs[4], scaleAxes.dimColor[4]);
		scaleAxes.model[5] = cCube(axisMins[5], axisMaxs[5], scaleAxes.dimColor[5]);

		// flip to HL coords
		cVert* verts = (cVert*)scaleAxes.model;
		for (int i = 0; i < 6*6*6; i++) {
			float tmp = verts[i].z;
			verts[i].z = -verts[i].y;
			verts[i].y = tmp;
		}

 		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		vec3 grabAxisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 grabAxisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};

		for (int i = 0; i < 6; i++) {
			scaleAxes.mins[i] = grabAxisMins[i];
			scaleAxes.maxs[i] = grabAxisMaxs[i];
		}
	}
	else {
		if (ent != NULL) {
			moveAxes.origin = getEntOrigin(map, ent);
		}

		// flipped for HL coords
		moveAxes.model[0] = cCube(vec3(0, -s, -s), vec3(d, s, s), moveAxes.dimColor[0]);
		moveAxes.model[2] = cCube(vec3(-s, 0, -s), vec3(s, d, s), moveAxes.dimColor[2]);
		moveAxes.model[1] = cCube(vec3(-s, -s, 0), vec3(s, s, -d), moveAxes.dimColor[1]);
		moveAxes.model[3] = cCube(vec3(-s2, -s2, -s2), vec3(s2, s2, s2), moveAxes.dimColor[3]);

		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		s2 *= 1.5f;

		activeAxes.mins[0] = vec3(0, -s, -s);
		activeAxes.mins[1] = vec3(-s, 0, -s);
		activeAxes.mins[2] = vec3(-s, -s, 0);
		activeAxes.mins[3] = vec3(-s2, -s2, -s2);

		activeAxes.maxs[0] = vec3(d, s, s);
		activeAxes.maxs[1] = vec3(s, d, s);
		activeAxes.maxs[2] = vec3(s, s, d);
		activeAxes.maxs[3] = vec3(s2, s2, s2);
	}
	

	if (draggingAxis >= 0 && draggingAxis < activeAxes.numAxes) {
		activeAxes.model[draggingAxis].setColor(activeAxes.hoverColor[draggingAxis]);
	}
	else if (hoverAxis >= 0 && hoverAxis < activeAxes.numAxes) {
		activeAxes.model[hoverAxis].setColor(activeAxes.hoverColor[hoverAxis]);
	}
	else if (gui->guiHoverAxis >= 0 && gui->guiHoverAxis < activeAxes.numAxes) {
		activeAxes.model[gui->guiHoverAxis].setColor(activeAxes.hoverColor[gui->guiHoverAxis]);
	}
}

vec3 Renderer::getAxisDragPoint(vec3 origin) {
	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	vec3 axisNormals[3] = {
		vec3(1,0,0),
		vec3(0,1,0),
		vec3(0,0,1)
	};

	// get intersection points between the pick ray and each each movement direction plane
	float dots[3];
	for (int i = 0; i < 3; i++) {
		dots[i] = fabs(dotProduct(cameraForward, axisNormals[i]));
	}

	// best movement planee is most perpindicular to the camera direction
	// and ignores the plane being moved
	int bestMovementPlane = 0;
	switch (draggingAxis % 3) {
		case 0: bestMovementPlane = dots[1] > dots[2] ? 1 : 2; break;
		case 1: bestMovementPlane = dots[0] > dots[2] ? 0 : 2; break;
		case 2: bestMovementPlane = dots[1] > dots[0] ? 1 : 0; break;
	}

	float fDist = ((float*)&origin)[bestMovementPlane];
	float intersectDist;
	rayPlaneIntersect(pickStart, pickDir, axisNormals[bestMovementPlane], fDist, intersectDist);

	// don't let ents zoom out to infinity
	if (intersectDist < 0) {
		intersectDist = 0;
	}

	return pickStart + pickDir * intersectDist;
}

void Renderer::updateScaleVerts(bool currentlyScaling) {
	if (!pickInfo.valid || pickInfo.modelIdx <= 0)
		return;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	int modelIdx = map->ents[pickInfo.entIdx]->getBspModelIdx();

	// persist scale change
	if (currentlyScaling) {
		for (int i = 0; i < numScaleVerts; i++) {
			scaleVertsStart[i] = *scaleVerts[i];
		}
		for (int i = 0; i < scalePlanes.size(); i++) {
			ScalablePlane& sp = scalePlanes[i];
			BSPPLANE& plane = map->planes[scalePlanes[i].planeIdx];

			vec3 ba = sp.v1 - sp.origin;
			vec3 va = sp.v2 - sp.origin;

			vec3 newNormal = crossProduct(ba.normalize(), va.normalize()).normalize();
			float newDist = getDistAlongAxis(newNormal, sp.origin);

			bool flipped = plane.update(newNormal, newDist);

			if (flipped) {
				for (int i = 0; i < map->faceCount; i++) {
					BSPFACE& face = map->faces[i];
					if (face.iPlane == sp.planeIdx) {
						face.nPlaneSide = !face.nPlaneSide;
					}
				}
				for (int i = 0; i < map->nodeCount; i++) {
					BSPNODE& node = map->nodes[i];
					if (node.iPlane == sp.planeIdx) {
						int16 temp = node.iChildren[0];
						node.iChildren[0] = node.iChildren[1];
						node.iChildren[1] = node.iChildren[0];
					}
				}
			}
		}
		for (int i = 0; i < scaleTexinfos.size(); i++) {
			BSPTEXTUREINFO& info = map->texinfos[scaleTexinfos[i].texinfoIdx];
			scaleTexinfos[i].oldShiftS = info.shiftS;
			scaleTexinfos[i].oldShiftT = info.shiftT;
			scaleTexinfos[i].oldS = info.vS;
			scaleTexinfos[i].oldT = info.vT;
		}
		BSPMODEL& model = map->models[modelIdx];
		map->get_model_vertex_bounds(modelIdx, model.nMins, model.nMaxs);
		map->regenerate_clipnodes(modelIdx);
		return;
	}

	if (scaleVertsStart != NULL) {
		delete[] scaleVertsStart;
		delete[] scaleVerts;
		delete[] scaleVertDists;
		scalePlanes.clear();
		scaleTexinfos.clear();
	}
	
	scaleVerts = map->getModelVerts(modelIdx, numScaleVerts);
	scalePlanes = map->getScalablePlanes(modelIdx);
	scaleTexinfos = map->getScalableTexinfos(modelIdx);
	int numScalePlaneVerts = scalePlanes.size() * 3;
	int totalScaleVerts = numScaleVerts + numScalePlaneVerts;

	vec3** newScaleVerts = new vec3 * [totalScaleVerts];
	memcpy(newScaleVerts, scaleVerts, numScaleVerts*sizeof(vec3*));
	delete[] scaleVerts;
	scaleVerts = newScaleVerts;

	for (int i = numScaleVerts, k = 0; i < totalScaleVerts; i += 3, k++) {
		ScalablePlane& sp = scalePlanes[k];
		scaleVerts[i + 0] = &sp.origin;
		scaleVerts[i + 1] = &sp.v1;
		scaleVerts[i + 2] = &sp.v2;
	}

	numScaleVerts = totalScaleVerts;

	scaleVertsStart = new vec3[totalScaleVerts];
	scaleVertDists = new float[totalScaleVerts];
	for (int i = 0; i < totalScaleVerts; i++) {
		scaleVertsStart[i] = *scaleVerts[i];
	}
}

void Renderer::scaleSelectedVerts(vec3 dir, vec3 fromDir) {
	if (!pickInfo.valid || pickInfo.modelIdx <= 0)
		return;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;

	vec3 n = fromDir.normalize(1.0f);

	float minAxisDist = 9e99;
	float maxAxisDist = -9e99;

	int faceVertCount = numScaleVerts - scalePlanes.size() * 3;

	for (int i = 0; i < numScaleVerts; i++) {
		float dist = getDistAlongAxis(n, scaleVertsStart[i]);

		if (i < faceVertCount) {
			if (dist > maxAxisDist) maxAxisDist = dist;
			if (dist < minAxisDist) minAxisDist = dist;
		}
		scaleVertDists[i] = dist;
	}

	float distRange = maxAxisDist - minAxisDist;

	for (int i = 0; i < numScaleVerts; i++) {
		float stretchFactor = (scaleVertDists[i] - minAxisDist) / distRange;
		*scaleVerts[i] = scaleVertsStart[i] + dir * stretchFactor;
	}

	//
	// TODO: I have no idea what I'm doing but this code usually scales axis-aligned texture coord axes correctly.
	//

	if (!textureLock)
		return;

	minAxisDist = 9e99;
	maxAxisDist = -9e99;
	
	for (int i = 0; i < faceVertCount; i++) {
		float dist = getDistAlongAxis(n, *scaleVerts[i]);
		if (dist > maxAxisDist) maxAxisDist = dist;
		if (dist < minAxisDist) minAxisDist = dist;
	}
	float newDistRange = maxAxisDist - minAxisDist;
	float scaleFactor = distRange / newDistRange;

	mat4x4 scaleMat;
	scaleMat.loadIdentity();
	vec3 asdf = dir.normalize();
	if (fabs(asdf.x) > 0) {
		scaleMat.scale(scaleFactor, 1, 1);
	}
	else if (fabs(asdf.z) > 0) {
		scaleMat.scale(1, 1, scaleFactor);
	}
	else {
		scaleMat.scale(1, scaleFactor, 1);
	}

	for (int i = 0; i < scaleTexinfos.size(); i++) {
		ScalableTexinfo& oldinfo = scaleTexinfos[i];
		BSPTEXTUREINFO& info = map->texinfos[scaleTexinfos[i].texinfoIdx];
		BSPPLANE& plane = map->planes[scaleTexinfos[i].planeIdx];

		info.vS = (scaleMat * vec4(oldinfo.oldS, 1)).xyz();
		info.vT = (scaleMat * vec4(oldinfo.oldT, 1)).xyz();

		float dotS = dotProduct(oldinfo.oldS.normalize(), dir.normalize());
		float dotT = dotProduct(oldinfo.oldT.normalize(), dir.normalize());

		float asdf = dotProduct(fromDir, info.vS) < 0 ? 1 : -1;
		float asdf2 = dotProduct(fromDir, info.vT) < 0 ? 1 : -1;

		float vsdiff = info.vS.length() - oldinfo.oldS.length();
		float vtdiff = info.vT.length() - oldinfo.oldT.length();

		info.shiftS = oldinfo.oldShiftS + (minAxisDist * vsdiff * fabs(dotS)) * asdf;
		info.shiftT = oldinfo.oldShiftT + (minAxisDist * vtdiff * fabs(dotT)) * asdf2;
	}
}

vec3 Renderer::snapToGrid(vec3 pos) {
	float snapSize = pow(2.0, gridSnapLevel);
	float halfSnap = snapSize * 0.5f;
	
	int x = round((pos.x) / snapSize) * snapSize;
	int y = round((pos.y) / snapSize) * snapSize;
	int z = round((pos.z) / snapSize) * snapSize;

	return vec3(x, y, z);
}

void Renderer::grabEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;
	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	grabDist = (getEntOrigin(map, map->ents[pickInfo.entIdx]) - cameraOrigin).length();
	grabStartOrigin = cameraOrigin + cameraForward * grabDist;
	gragStartEntOrigin = cameraOrigin + cameraForward * grabDist;
}

void Renderer::cutEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	if (copiedEnt != NULL)
		delete copiedEnt;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	copiedEnt = new Entity();
	*copiedEnt = *map->ents[pickInfo.entIdx];
	delete map->ents[pickInfo.entIdx];
	map->ents.erase(map->ents.begin() + pickInfo.entIdx);
	mapRenderers[pickInfo.mapIdx]->preRenderEnts();
	pickInfo.valid = false;
}

void Renderer::copyEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	if (copiedEnt != NULL)
		delete copiedEnt;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	copiedEnt = new Entity();
	*copiedEnt = *map->ents[pickInfo.entIdx];
}

void Renderer::pasteEnt(bool noModifyOrigin) {
	if (copiedEnt == NULL)
		return;

	Bsp* map = getMapContainingCamera()->map;

	Entity* insertEnt = new Entity();
	*insertEnt = *copiedEnt;

	if (!noModifyOrigin) {
		// can't just set camera origin directly because solid ents can have (0,0,0) origins
		vec3 oldOrigin = getEntOrigin(map, insertEnt);
		vec3 modelOffset = getEntOffset(map, insertEnt);

		vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
		vec3 newOri = (oldOrigin + moveDist) - modelOffset;
		vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
		insertEnt->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
	}

	map->ents.push_back(insertEnt);

	pickInfo.entIdx = map->ents.size() - 1;
	pickInfo.valid = true;
	mapRenderers[pickInfo.mapIdx]->preRenderEnts();
}

void Renderer::deleteEnt() {
	if (!pickInfo.valid || pickInfo.entIdx <= 0)
		return;

	Bsp* map = mapRenderers[pickInfo.mapIdx]->map;
	map->ents.erase(map->ents.begin() + pickInfo.entIdx);
	mapRenderers[pickInfo.mapIdx]->preRenderEnts();
	pickInfo.valid = false;
}