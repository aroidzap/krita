/*
 *  SPDX-FileCopyrightText: 2011 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "move_stroke_strategy.h"

#include <klocalizedstring.h>
#include "kis_image_interfaces.h"
#include "kis_node.h"
#include "commands_new/kis_update_command.h"
#include "commands_new/kis_node_move_command2.h"
#include "kis_layer_utils.h"
#include "krita_utils.h"

#include "KisRunnableStrokeJobData.h"
#include "KisRunnableStrokeJobUtils.h"
#include "KisRunnableStrokeJobsInterface.h"
#include "kis_abstract_projection_plane.h"
#include "kis_image.h"

#include "kis_transform_mask.h"
#include "kis_transform_mask_params_interface.h"
#include "kis_keyframe_channel.h"
#include "kis_scalar_keyframe_channel.h"
#include "kis_image_animation_interface.h"
#include "commands_new/KisSimpleModifyTransformMaskCommand.h"

MoveStrokeStrategy::MoveStrokeStrategy(KisNodeSelectionRecipe nodeSelection,
                                       KisUpdatesFacade *updatesFacade,
                                       KisStrokeUndoFacade *undoFacade)
    : KisStrokeStrategyUndoCommandBased(kundo2_i18n("Move"), false, undoFacade),
      m_requestedNodeSelection(nodeSelection),
      m_updatesFacade(updatesFacade),
      m_updatesEnabled(true)
{
    setSupportsWrapAroundMode(true);

    enableJob(KisSimpleStrokeStrategy::JOB_INIT, true, KisStrokeJobData::BARRIER);
}

MoveStrokeStrategy::MoveStrokeStrategy(KisNodeList nodes, KisUpdatesFacade *updatesFacade, KisStrokeUndoFacade *undoFacade)
    : MoveStrokeStrategy(KisNodeSelectionRecipe(nodes), updatesFacade, undoFacade)
{
}

MoveStrokeStrategy::MoveStrokeStrategy(const MoveStrokeStrategy &rhs, int lod)
    : QObject(),
      KisStrokeStrategyUndoCommandBased(rhs),
      m_requestedNodeSelection(rhs.m_requestedNodeSelection, lod),
      m_nodes(rhs.m_nodes),
      m_blacklistedNodes(rhs.m_blacklistedNodes),
      m_updatesFacade(rhs.m_updatesFacade),
      m_finalOffset(rhs.m_finalOffset),
      m_dirtyRect(rhs.m_dirtyRect),
      m_dirtyRects(rhs.m_dirtyRects),
      m_updatesEnabled(rhs.m_updatesEnabled),
      m_transformMaskData()
{
    KIS_SAFE_ASSERT_RECOVER_NOOP(rhs.m_transformMaskData.empty());
}
void MoveStrokeStrategy::saveInitialNodeOffsets(KisNodeSP node)
{
    if (!m_blacklistedNodes.contains(node)) {
        m_initialNodeOffsets.insert(node, QPoint(node->x(), node->y()));
    }

    KisNodeSP child = node->firstChild();
    while(child) {
        saveInitialNodeOffsets(child);
        child = child->nextSibling();
    }
}

void MoveStrokeStrategy::initStrokeCallback()
{
    /**
     * Our LodN moght have already prepared the list of nodes for us,
     * so we should reuse it to avoid different nodes to be moved in
     * LodN and Lod0 modes.
     */
    if (m_updatesEnabled) {
        m_nodes = m_requestedNodeSelection.selectNodesToProcess();

        if (!m_nodes.isEmpty()) {
            m_nodes = KisLayerUtils::sortAndFilterMergableInternalNodes(m_nodes, true);
        }

        KritaUtils::filterContainer<KisNodeList>(m_nodes,
                                                 [this](KisNodeSP node) {
            // TODO: check isolation
            return
                    !KisLayerUtils::checkIsCloneOf(node, m_nodes) &&
                    node->isEditable(true);
        });
        Q_FOREACH(KisNodeSP subtree, m_nodes) {
            KisLayerUtils::recursiveApplyNodes(
                        subtree,
                        [this](KisNodeSP node) {
                if (KisLayerUtils::checkIsCloneOf(node, m_nodes) ||
                        !node->isEditable(false)) {

                    m_blacklistedNodes.insert(node);
                }
            });
        }

        if (m_sharedNodes) {
            *m_sharedNodes = m_nodes;
        }
    } else {
        KIS_SAFE_ASSERT_RECOVER_RETURN(m_sharedNodes);
        m_nodes = *m_sharedNodes;
    }

    if (m_nodes.isEmpty()) {
        emit sigStrokeStartedEmpty();
        return;
    }

    QVector<KisRunnableStrokeJobData*> jobs;

    KritaUtils::addJobBarrier(jobs, [this]() {
        Q_FOREACH(KisNodeSP node, m_nodes) {
            KisLayerUtils::forceAllHiddenOriginalsUpdate(node);
        }
    });

    KritaUtils::addJobBarrier(jobs, [this]() {
        Q_FOREACH(KisNodeSP node, m_nodes) {
            KisLayerUtils::forceAllDelayedNodesUpdate(node);
        }
    });

    KritaUtils::addJobBarrier(jobs, [this]() {
        QRect handlesRect;

        Q_FOREACH(KisNodeSP node, m_nodes) {
            saveInitialNodeOffsets(node);
            handlesRect |= KisLayerUtils::recursiveTightNodeVisibleBounds(node);
        }

        KisStrokeStrategyUndoCommandBased::initStrokeCallback();

        if (m_updatesEnabled) {
            KisLodTransform t(m_nodes.first()->image()->currentLevelOfDetail());
            handlesRect = t.mapInverted(handlesRect);

            emit this->sigHandlesRectCalculated(handlesRect);
        }

        m_updateTimer.start();
    });

    runnableJobsInterface()->addRunnableJobs(jobs);
}

void MoveStrokeStrategy::finishStrokeCallback()
{
    Q_FOREACH (KisNodeSP node, m_nodes) {
        KUndo2Command *updateCommand =
            new KisUpdateCommand(node, m_dirtyRects[node], m_updatesFacade, true);

        addMoveCommands(node, updateCommand);

        notifyCommandDone(KUndo2CommandSP(updateCommand),
                          KisStrokeJobData::SEQUENTIAL,
                          KisStrokeJobData::EXCLUSIVE);
    }

    if (!m_updatesEnabled) {
        Q_FOREACH (KisNodeSP node, m_nodes) {
            m_updatesFacade->refreshGraphAsync(node, m_dirtyRects[node]);
        }
    }

    KisStrokeStrategyUndoCommandBased::finishStrokeCallback();
}

void MoveStrokeStrategy::cancelStrokeCallback()
{
    if (!m_nodes.isEmpty()) {
        m_finalOffset = QPoint();
        m_hasPostponedJob = true;
        tryPostUpdateJob(true);
    }

    KisStrokeStrategyUndoCommandBased::cancelStrokeCallback();
}

void MoveStrokeStrategy::tryPostUpdateJob(bool forceUpdate)
{
    if (!m_hasPostponedJob) return;

    if (forceUpdate ||
        (m_updateTimer.elapsed() > m_updateInterval &&
         !m_updatesFacade->hasUpdatesRunning())) {

        addMutatedJob(new BarrierUpdateData(forceUpdate));
    }
}

void MoveStrokeStrategy::doStrokeCallback(KisStrokeJobData *data)
{
    if (PickLayerData *pickData = dynamic_cast<PickLayerData*>(data)) {
        KisNodeSelectionRecipe clone = m_requestedNodeSelection;
        clone.pickPoint = pickData->pos;
        emit sigLayersPicked(clone.selectNodesToProcess());
        return;
    }

    Data *d = dynamic_cast<Data*>(data);

    if (!m_nodes.isEmpty() && d) {
        /**
         * NOTE: we do not care about threading here, because
         * all our jobs are declared sequential
         */
        m_finalOffset = d->offset;
        m_hasPostponedJob = true;
        tryPostUpdateJob(false);

    } else if (BarrierUpdateData *barrierData =
               dynamic_cast<BarrierUpdateData*>(data)) {

        doCanvasUpdate(barrierData->forceUpdate);

    } else if (KisAsyncronousStrokeUpdateHelper::UpdateData *updateData =
               dynamic_cast<KisAsyncronousStrokeUpdateHelper::UpdateData*>(data)) {

        tryPostUpdateJob(updateData->forceUpdate);

    } else {
        KisStrokeStrategyUndoCommandBased::doStrokeCallback(data);
    }
}

#include "kis_selection_mask.h"
#include "kis_selection.h"

void MoveStrokeStrategy::doCanvasUpdate(bool forceUpdate)
{
    if (!forceUpdate &&
            (m_updateTimer.elapsed() < m_updateInterval ||
             m_updatesFacade->hasUpdatesRunning())) {

        return;
    }

    if (!m_hasPostponedJob) return;

    Q_FOREACH (KisNodeSP node, m_nodes) {
        QRect dirtyRect = moveNode(node, m_finalOffset);
        m_dirtyRects[node] |= dirtyRect;

        if (m_updatesEnabled) {
            m_updatesFacade->refreshGraphAsync(node, dirtyRect);
        }

        if (KisSelectionMask *mask = dynamic_cast<KisSelectionMask*>(node.data())) {
            Q_UNUSED(mask);
            //mask->selection()->notifySelectionChanged();
        }
    }

    m_hasPostponedJob = false;
    m_updateTimer.restart();
}


QRect MoveStrokeStrategy::moveNode(KisNodeSP node, QPoint offset)
{
    QRect dirtyRect;

    if (!m_blacklistedNodes.contains(node)) {
        dirtyRect = node->projectionPlane()->tightUserVisibleBounds();

        KisTransformMask *mask = dynamic_cast<KisTransformMask*>(node.data());
        if (mask && !KisLayerUtils::checkIsChildOf(node, m_nodes)) {

            TransformMaskData &data = m_transformMaskData[node];

            std::unique_ptr<KUndo2Command> cmd;

            KisTransformMaskParamsInterfaceSP oldParams = mask->transformParams();
            KisTransformMaskParamsInterfaceSP params = oldParams->clone();
            params->translateDstSpace(offset - data.currentOffset);

            if (mask->isAnimated()) {
                KUndo2Command* parent = new KUndo2Command();
                KisAnimatedTransformParamsInterface* animInterface = dynamic_cast<KisAnimatedTransformParamsInterface*>(mask->transformParams().data());
                KIS_ASSERT(animInterface);
                animInterface->initializeKeyframes(mask, params, parent);
                cmd.reset(parent);
            } else {
                mask->setTransformParams(params);
                cmd.reset(new KisSimpleModifyTransformMaskCommand(mask, oldParams, params));
            }

            KIS_ASSERT(cmd);

            if (data.undoCommand && !mask->isAnimated()) {
                const bool mergeResult = data.undoCommand->mergeWith(cmd.get());
                KIS_SAFE_ASSERT_RECOVER_NOOP(mergeResult);
                cmd.reset();
            } else {
                std::swap(data.undoCommand, cmd);
            }

            data.currentOffset = offset;

            dirtyRect |= node->projectionPlane()->tightUserVisibleBounds();

        } else {
            QPoint newOffset = m_initialNodeOffsets[node] + offset;

            /**
             * Some layers, e.g. clones need an update to change extent(), so
             * calculate the dirty rect manually
             */
            QPoint currentOffset(node->x(), node->y());
            dirtyRect |= dirtyRect.translated(newOffset - currentOffset);


            node->setX(newOffset.x());
            node->setY(newOffset.y());
        }
        KisNodeMoveCommand2::tryNotifySelection(node);
    }

    KisNodeSP child = node->firstChild();
    while(child) {
        dirtyRect |= moveNode(child, offset);
        child = child->nextSibling();
    }

    return dirtyRect;
}

void MoveStrokeStrategy::addMoveCommands(KisNodeSP node, KUndo2Command *parent)
{
    if (!m_blacklistedNodes.contains(node)) {
        if (m_transformMaskData.find(node) == m_transformMaskData.end()) {
            QPoint nodeOffset(node->x(), node->y());
            new KisNodeMoveCommand2(node, nodeOffset - m_finalOffset, nodeOffset, parent);
        } else {
            KisCommandUtils::CompositeCommand *cmd = new KisCommandUtils::CompositeCommand(parent);
            cmd->addCommand(m_transformMaskData[node].undoCommand.release());
        }
    }

    KisNodeSP child = node->firstChild();
    while(child) {
        addMoveCommands(child, parent);
        child = child->nextSibling();
    }
}

void MoveStrokeStrategy::setUpdatesEnabled(bool value)
{
    m_updatesEnabled = value;
}

bool checkSupportsLodMoves(KisNodeSP subtree)
{
    return
        !KisLayerUtils::recursiveFindNode(
            subtree,
            [](KisNodeSP node) -> bool {
                return !node->supportsLodMoves();
            });
}


KisStrokeStrategy* MoveStrokeStrategy::createLodClone(int levelOfDetail)
{
    KisNodeList nodesToCheck;

    if (m_requestedNodeSelection.mode == KisNodeSelectionRecipe::SelectedLayer) {
        nodesToCheck = m_requestedNodeSelection.selectedNodes;
    } else if (!m_requestedNodeSelection.selectedNodes.isEmpty()){
        /**
         * Since this function is executed in the GUI thread, we cannot properly
         * pick the layers. Therefore we should use pessimistic approach and
         * check if there are non-lodn-capable nodes in the entire image.
         */
        nodesToCheck.append(KisLayerUtils::findRoot(m_requestedNodeSelection.selectedNodes.first()));
    }

    Q_FOREACH (KisNodeSP node, nodesToCheck) {
        if (!checkSupportsLodMoves(node)) return 0;
    }

    MoveStrokeStrategy *clone = new MoveStrokeStrategy(*this, levelOfDetail);
    connect(clone, SIGNAL(sigHandlesRectCalculated(QRect)), this, SIGNAL(sigHandlesRectCalculated(QRect)));
    connect(clone, SIGNAL(sigStrokeStartedEmpty()), this, SIGNAL(sigStrokeStartedEmpty()));
    connect(clone, SIGNAL(sigLayersPicked(const KisNodeList&)), this, SIGNAL(sigLayersPicked(const KisNodeList&)));
    this->setUpdatesEnabled(false);
    m_sharedNodes.reset(new KisNodeList());
    clone->m_sharedNodes = m_sharedNodes;
    return clone;
}

MoveStrokeStrategy::Data::Data(QPoint _offset)
    : KisStrokeJobData(SEQUENTIAL, NORMAL),
      offset(_offset)
{
}

KisStrokeJobData *MoveStrokeStrategy::Data::createLodClone(int levelOfDetail)
{
    return new Data(*this, levelOfDetail);
}

MoveStrokeStrategy::Data::Data(const MoveStrokeStrategy::Data &rhs, int levelOfDetail)
    : KisStrokeJobData(rhs)
{
    KisLodTransform t(levelOfDetail);
    offset = t.map(rhs.offset);
}

MoveStrokeStrategy::PickLayerData::PickLayerData(QPoint _pos)
    : KisStrokeJobData(SEQUENTIAL, NORMAL),
      pos(_pos)
{
}

KisStrokeJobData *MoveStrokeStrategy::PickLayerData::createLodClone(int levelOfDetail) {
    return new PickLayerData(*this, levelOfDetail);
}

MoveStrokeStrategy::PickLayerData::PickLayerData(const MoveStrokeStrategy::PickLayerData &rhs, int levelOfDetail)
    : KisStrokeJobData(rhs)
{
    KisLodTransform t(levelOfDetail);
    pos = t.map(rhs.pos);
}

MoveStrokeStrategy::BarrierUpdateData::BarrierUpdateData(bool _forceUpdate)
    : KisAsyncronousStrokeUpdateHelper::UpdateData(_forceUpdate, BARRIER, EXCLUSIVE)
{}

KisStrokeJobData *MoveStrokeStrategy::BarrierUpdateData::createLodClone(int levelOfDetail) {
    return new BarrierUpdateData(*this, levelOfDetail);
}

MoveStrokeStrategy::BarrierUpdateData::BarrierUpdateData(const MoveStrokeStrategy::BarrierUpdateData &rhs, int levelOfDetail)
    : KisAsyncronousStrokeUpdateHelper::UpdateData(rhs, levelOfDetail)
{
}
